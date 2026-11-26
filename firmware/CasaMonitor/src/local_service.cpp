#include "local_service.h"
#include "crypto_util.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <cJSON.h>
#include <algorithm>
#include <set>
#include <mdns.h>

namespace casa {
namespace {
struct Node {LanDevice endpoint;std::string model,status="In attesa",challenge;unsigned nonceCount=0;bool verified=false;uint32_t attempted=0;bool tried=false;uint32_t retryMs=1000;};
class Response:public Stream {
public:
    std::string data;uint32_t started=millis();
    size_t write(uint8_t b)override{return write(&b,1);}
    size_t write(const uint8_t *v,size_t n)override{if(data.size()+n>24576||uint32_t(millis()-started)>2000)return 0;data.append(reinterpret_cast<const char *>(v),n);return n;}
    int available()override{return 0;}int read()override{return -1;}int peek()override{return -1;}void flush()override{}
};
int get(Node &node,const char *path,bool identity,std::string &body){
    if(node.endpoint.host.empty()||!localHostAllowed(node.endpoint.host))return -1;
    for(unsigned attempt=0;attempt<2;++attempt){
        NetworkClient client;HTTPClient http;http.setConnectTimeout(400);http.setTimeout(700);http.setReuse(false);http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
        const char *keys[]={"WWW-Authenticate"};http.collectHeaders(keys,1);
        auto url="http://"+node.endpoint.host+path;
        if(!http.begin(client,url.c_str()))return -1;
        if(!identity&&node.verified&&!node.endpoint.password.empty()){
            if(node.endpoint.generation==1)http.setAuthorization(node.endpoint.user.c_str(),node.endpoint.password.c_str());
            else if(!node.challenge.empty()){
                auto authorization=digestAuthorization(node.challenge,"GET",path,node.endpoint.user,node.endpoint.password,std::to_string(esp_random()),++node.nonceCount);
                if(authorization.empty()){http.end();return 401;}http.addHeader("Authorization",authorization.c_str());
            }
        }
        int code=http.GET();
        if(code==401&&!identity&&attempt==0&&node.endpoint.generation!=1&&!node.endpoint.password.empty()){
            node.challenge=http.header("WWW-Authenticate").c_str();node.nonceCount=0;http.end();continue;
        }
        if(code==200){Response response;int size=http.getSize();if(size>24576||http.writeToStream(&response)<=0)code=-1;else body=std::move(response.data);}
        http.end();return code;
    }
    return 401;
}
bool identify(Node &node){
    node.verified=false;
    std::string body;int generation=0;std::string model;
    if(get(node,"/shelly",true,body)!=200){node.status="Non raggiungibile";return false;}
    if(!parseLocalIdentity(body.c_str(),node.endpoint.id,generation,model)){node.status="Identificativo diverso: host rifiutato";return false;}
    if(node.endpoint.id.empty()){
        auto *root=cJSON_Parse(body.c_str());auto *id=cJSON_GetObjectItemCaseSensitive(root,"mac");if(!cJSON_IsString(id))id=cJSON_GetObjectItemCaseSensitive(root,"id");
        node.endpoint.id=cJSON_IsString(id)?normalizedDeviceId(id->valuestring):"";cJSON_Delete(root);
    }
    node.endpoint.generation=generation;node.model=model=="SHEM"?"Shelly EM":model;
    if(node.model.empty())node.model=generation==1?"Shelly Gen1":"Shelly Gen"+std::to_string(generation);
    node.verified=true;node.status="Identificato";return true;
}
void addCatalog(const Node &node,std::vector<MeterSource> &catalog){
    auto add=[&](const char *channel){for(const auto &s:catalog)if(s.device==node.endpoint.id&&s.source==channel)return;catalog.push_back({node.model,node.endpoint.id,channel,-1});};
    if(node.model=="Shelly EM"){add("emeters[0]");add("emeters[1]");}
    else if(node.endpoint.generation>=2&&(node.model.find("SW")!=std::string::npos||node.model.find("PM")!=std::string::npos))add("switch:0");
}
}
void LocalService::begin(){mutex=xSemaphoreCreateMutex();if(xTaskCreatePinnedToCore(entry,"ShellyLAN",6144,this,1,&workerHandle,1)!=pdPASS)Serial.println("Shelly LAN: memoria insufficiente per avviare il task");}
void LocalService::configure(const DeviceOptions &c,const EnergyConfig &e){xSemaphoreTake(mutex,portMAX_DELAY);options=c;energy=e;++generation;readings={};views.clear();catalog.clear();xSemaphoreGive(mutex);scanRequested.store(true);}
void LocalService::snapshot(RawReadings &r,std::vector<LocalDeviceView> &v,std::vector<MeterSource> &c){xSemaphoreTake(mutex,portMAX_DELAY);r=readings;v=views;c=catalog;xSemaphoreGive(mutex);}
void LocalService::entry(void *self){static_cast<LocalService *>(self)->task();}
void LocalService::task(){
    uint32_t observed=0,lastScan=0;bool mdns=false;std::vector<Node> nodes;RawReadings raw{};std::vector<MeterSource> inventory;std::vector<std::string> candidates;
    for(;;){
        DeviceOptions cfg;EnergyConfig plan;uint32_t gen;
        xSemaphoreTake(mutex,portMAX_DELAY);cfg=options;plan=energy;gen=generation;xSemaphoreGive(mutex);
        if(gen!=observed){observed=gen;nodes.clear();raw={};inventory.clear();std::set<std::string> ids;
            for(const auto &s:plan.sources)if(!s.device.empty())ids.insert(s.device);
            for(const auto &d:cfg.devices)ids.insert(d.id);
            for(const auto &id:ids){Node n;n.endpoint.id=id;for(const auto &d:cfg.devices)if(d.id==id)n.endpoint=d;nodes.push_back(n);}
        }
        if(!cfg.localEnabled||WiFi.status()!=WL_CONNECTED){if(mdns&&WiFi.status()!=WL_CONNECTED){MDNS.end();mdns=false;}vTaskDelay(pdMS_TO_TICKS(250));continue;}
        if(!mdns)mdns=MDNS.begin("casa-energia");
        if(scanRequested.exchange(false)||uint32_t(millis()-lastScan)>=60000){lastScan=millis();
            for(const char *service:{"_http","_shelly"}){
                mdns_result_t *results=nullptr;mdns_query_ptr(service,"_tcp",650,16,&results);
                for(auto *r=results;r;r=r->next){
                    if(!r->hostname||std::string(r->hostname).rfind("shelly",0)!=0||r->port!=80)continue;
                    for(auto *a=r->addr;a;a=a->next)if(a->addr.type==ESP_IPADDR_TYPE_V4){
                        auto host=std::string(IPAddress(a->addr.u_addr.ip4.addr).toString().c_str());
                        if(localHostAllowed(host)&&candidates.size()<16&&std::find(candidates.begin(),candidates.end(),host)==candidates.end())candidates.push_back(host);
                    }
                }mdns_query_results_free(results);
            }
        }
        // Limit identification work between successive rounds of meter reads.
        if(!candidates.empty()){
            Node candidate;candidate.endpoint.host=candidates.back();candidates.pop_back();
            if(identify(candidate)){
                auto it=std::find_if(nodes.begin(),nodes.end(),[&](const Node &n){return n.endpoint.id==candidate.endpoint.id;});
                if(it==nodes.end()){if(nodes.size()<7){nodes.push_back(candidate);addCatalog(candidate,inventory);}}
                else {bool manual=false;for(const auto &d:cfg.devices)if(d.id==it->endpoint.id&&!d.host.empty())manual=true;
                    if(!manual){it->endpoint.host=candidate.endpoint.host;it->endpoint.generation=candidate.endpoint.generation;it->model=candidate.model;it->verified=true;it->tried=false;}addCatalog(*it,inventory);
                }
            }
        }
        for(auto &node:nodes){
            auto now=millis();if(node.tried&&uint32_t(now-node.attempted)<node.retryMs)continue;node.attempted=now;node.tried=true;
            if(node.endpoint.host.empty()){node.status="Ricerca automatica in corso";node.retryMs=10000;continue;}
            if(!identify(node)){node.retryMs=10000;continue;}
            addCatalog(node,inventory);
            bool needed=false;for(const auto &s:plan.sources)if(s.device==node.endpoint.id)needed=true;
            if(!needed){node.status="Trovato · non associato";node.retryMs=60000;continue;}
            std::string body;int code=get(node,node.endpoint.generation==1?"/status":"/rpc/Shelly.GetStatus",false,body);
            node.retryMs=cfg.pollMs;
            if(code==200&&parseLocalStatus(body.c_str(),node.endpoint.id,node.endpoint.generation,plan,millis(),cfg.localStaleMs,raw))node.status="Locale collegato";
            else {node.status=code==401?"Password necessaria o errata":"Lettura locale non riuscita";node.retryMs=code==401?30000:cfg.pollMs;}
        }
        std::vector<LocalDeviceView> v;for(const auto &n:nodes)v.push_back({n.endpoint,n.model,n.status});
        xSemaphoreTake(mutex,portMAX_DELAY);if(gen==generation){readings=raw;views=std::move(v);catalog=inventory;}xSemaphoreGive(mutex);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
}
