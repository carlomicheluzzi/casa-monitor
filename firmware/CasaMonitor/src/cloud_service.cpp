#include "cloud_service.h"
#include "version.h"
#include "tls_memory.h"
#include "device_platform.h"
#include <cmath>
#include <WiFi.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <Preferences.h>
#include <cJSON.h>
#include <set>
#include <algorithm>
#include <sys/time.h>
#include <esp_heap_caps.h>

extern const uint8_t rootBundleStart[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t rootBundleEnd[] asm("_binary_x509_crt_bundle_end");

namespace casa {
namespace {
const cJSON *get(const cJSON *o,const char *key) {return cJSON_GetObjectItemCaseSensitive(o,key);}
std::string str(const cJSON *o) {return cJSON_IsString(o)&&o->valuestring?o->valuestring:"";}
bool serverAllowed(const std::string &s) {
    const std::string prefix="https://",suffix=".shelly.cloud";
    if(s.rfind(prefix,0)!=0||s.size()>120||s.size()<=prefix.size()+suffix.size()||
       s.compare(s.size()-suffix.size(),suffix.size(),suffix)!=0) return false;
    return std::all_of(s.begin()+prefix.size(),s.end(),[](char c){return(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'||c=='.';});
}
std::string urlEncode(const std::string &s) {
    static const char *hex="0123456789ABCDEF"; std::string out;
    for(unsigned char c:s) {
        if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c=='~') out+=c;
        else {out+='%';out+=hex[c>>4];out+=hex[c&15];}
    }
    return out;
}
std::string jsonText(cJSON *value) {
    char *text=cJSON_PrintUnformatted(value); std::string result=text?text:""; cJSON_free(text); return result;
}
class LimitedStream:public Stream {
public:
    std::string data;
    size_t write(uint8_t v) override {return write(&v,1);}
    size_t write(const uint8_t *buffer,size_t size) override {
        if(data.size()+size>32768) return 0;
        data.append(reinterpret_cast<const char *>(buffer),size); return size;
    }
    int available() override {return 0;} int read() override {return -1;} int peek() override {return -1;} void flush() override {}
};
}
std::string CloudService::serialize(const Settings &s) {
    auto *root=cJSON_CreateObject();
    cJSON_AddStringToObject(root,"server",s.server.c_str()); cJSON_AddStringToObject(root,"auth_key",s.key.c_str());
    auto energy=encodeEnergyConfig(s.energy);
    cJSON_AddItemToObject(root,"config",cJSON_Parse(energy.c_str()));
    auto *list=cJSON_AddArrayToObject(root,"catalog");
    for(const auto &source:s.catalog) {
        auto *v=cJSON_CreateObject();
        cJSON_AddStringToObject(v,"name",source.name.c_str()); cJSON_AddStringToObject(v,"device",source.device.c_str());
        cJSON_AddStringToObject(v,"source",source.source.c_str()); cJSON_AddItemToArray(list,v);
    }
    auto out=jsonText(root); cJSON_Delete(root); return out;
}
bool CloudService::configure(const char *text,bool persist) {
    auto *root=cJSON_Parse(text); if(!root) return false;
    Settings candidate;
    candidate.server=str(get(root,"server")); candidate.key=str(get(root,"auth_key"));
    auto *cfg=get(root,"config");
    auto energy=jsonText(const_cast<cJSON *>(cfg));
    bool valid=serverAllowed(candidate.server)&&candidate.key.size()<=512&&
        std::all_of(candidate.key.begin(),candidate.key.end(),[](unsigned char c){return c>=33&&c<127;})&&
        parseEnergyConfig(energy.c_str(),candidate.energy);
    auto *list=get(root,"catalog");
    if(cJSON_IsArray(list)&&cJSON_GetArraySize(list)<=32) {
        for(auto *v=list->child;v;v=v->next) {
            MeterSource item{str(get(v,"name")),str(get(v,"device")),str(get(v,"source")),-1};
            EnergyConfig check; check.sources[4]=item; check.sources[4].zone=0;
            // Validate a catalogue item independently from the four configured mains.
            bool known=false;
            for(const auto &s:candidate.energy.sources) if(s.device==item.device&&s.source==item.source) known=true;
            if(item.name.size()<=64&&!item.name.empty()&&(known||validateEnergyConfig(check).empty())) candidate.catalog.push_back(item);
        }
    }
    if(candidate.catalog.empty()) for(size_t i=0;i<4;++i) candidate.catalog.push_back(candidate.energy.sources[i]);
    double epoch=cJSON_IsNumber(get(root,"unix_time"))?get(root,"unix_time")->valuedouble:0;
    cJSON_Delete(root);
    if(!valid) return false;
    if(persist) {
        auto encoded=serialize(candidate); Preferences storage;
        if(encoded.empty()||!storage.begin("casa-cloud",false)) return false;
        bool ok=storage.putString("config",encoded.c_str())==encoded.size(); storage.end();
        if(!ok) return false;
    }
    xSemaphoreTake(mutex,portMAX_DELAY);
    settings=candidate; ++generation; haveResult=false;
    xSemaphoreGive(mutex);
    live.configure(candidate.energy); catalog=candidate.catalog; ready=true;cloudRaw={};cloudOK=false;lastCloudError.clear();
    if(persist&&epoch>1735689600&&epoch<4102444800) {
        timeval tv{time_t(epoch),0}; settimeofday(&tv,nullptr);
    }
    return true;
}
void CloudService::begin() {
    mutex=xSemaphoreCreateMutex();
    Preferences storage;
    if(storage.begin("casa-cloud",true)) {
        auto value=storage.getString("config",""); storage.end();
        if(!value.isEmpty()) configure(value.c_str(),false);
    }
    if(!ready) live.fail("Configura Shelly tramite USB",millis());
    // The RGB bounce-buffer ISR and LVGL run on core 1. Keep TLS/PSRAM work
    // on that core at lower priority, so the ISR can preempt it instead of
    // competing with memory-intensive TLS work on the other core.
    workerReady=xTaskCreatePinnedToCore(taskEntry,"ShellyCloud",12288,this,1,&workerHandle,1)==pdPASS;
    if(!workerReady)
        live.fail("Memoria insufficiente per Shelly Cloud",millis());
}
bool CloudService::saveConfig(const EnergyConfig &config) {
    if(!validateEnergyConfig(config).empty()) return false;
    xSemaphoreTake(mutex,portMAX_DELAY); auto copy=settings; xSemaphoreGive(mutex);
    if(copy.server.empty())copy.server="https://shelly-52-eu.shelly.cloud";
    copy.catalog=catalog;copy.energy=config; return configure(serialize(copy).c_str(),true);
}
bool CloudService::schedule(void (*work)(void *),void *context){xSemaphoreTake(mutex,portMAX_DELAY);bool ok=workerReady&&!jobBusy;if(ok){job=work;jobContext=context;jobBusy=true;}xSemaphoreGive(mutex);return ok;}
void CloudService::mergeCatalog(const std::vector<MeterSource> &sources){for(const auto &s:sources){bool found=false;for(const auto &v:catalog)if(v.device==s.device&&v.source==s.source)found=true;if(!found&&catalog.size()<32)catalog.push_back(s);}}
std::string CloudService::serverName() const {return ready?settings.server:"Da configurare via USB";}
void CloudService::taskEntry(void *self) {registerTlsMemoryTask();static_cast<CloudService *>(self)->task();}
void CloudService::post(Result result) {
    xSemaphoreTake(mutex,portMAX_DELAY);
    if(result.generation==generation) {pending=std::move(result);haveResult=true;}
    xSemaphoreGive(mutex);
}
void CloudService::task() {
    uint32_t attempted=0,interval=3000,observedGeneration=0;
    bool madeRequest=false,clockStarted=false;
    NetworkClientSecure client;
    client.setCACertBundle(rootBundleStart,size_t(rootBundleEnd-rootBundleStart));
    client.setHandshakeTimeout(15);
    HTTPClient http;
    http.setTimeout(15000); http.setConnectTimeout(10000);
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    http.setReuse(true);
    auto closeTransport=[&]() {
        http.setReuse(false);http.end();client.stop();http.setReuse(true);
    };
    for(;;) {
        void (*work)(void *)=nullptr;void *context=nullptr;
        xSemaphoreTake(mutex,portMAX_DELAY);work=job;context=jobContext;job=nullptr;xSemaphoreGive(mutex);
        if(work){pauseConfirmed.store(false);closeTransport();work(context);xSemaphoreTake(mutex,portMAX_DELAY);jobBusy=false;xSemaphoreGive(mutex);continue;}
        if(pauseRequested.load()){closeTransport();pauseConfirmed.store(true);vTaskDelay(pdMS_TO_TICKS(100));continue;}
        pauseConfirmed.store(false);
        if(!cloudNeeded.load()){closeTransport();vTaskDelay(pdMS_TO_TICKS(250));continue;}
        Settings cfg; uint32_t gen;
        xSemaphoreTake(mutex,portMAX_DELAY); cfg=settings; gen=generation; xSemaphoreGive(mutex);
        if(gen!=observedGeneration) {closeTransport();observedGeneration=gen;madeRequest=false;interval=3000;}
        if(cfg.key.empty()||WiFi.status()!=WL_CONNECTED) {closeTransport();vTaskDelay(pdMS_TO_TICKS(500));continue;}
        if(!clockStarted) {configTime(0,0,"pool.ntp.org","time.cloudflare.com");clockStarted=true;}
        if(time(nullptr)<1735689600) {
            post({false,{},"Sincronizzo l’ora per HTTPS…",gen});vTaskDelay(pdMS_TO_TICKS(1000));continue;
        }
        if(madeRequest&&uint32_t(millis()-attempted)<interval) {vTaskDelay(pdMS_TO_TICKS(100));continue;}
        madeRequest=true; attempted=millis();
        Result result; result.generation=gen;
        std::set<std::string> ids;
        for(const auto &s:cfg.energy.sources) if(!s.device.empty()) ids.insert(s.device);
        auto *body=cJSON_CreateObject(),*list=cJSON_AddArrayToObject(body,"ids");
        for(const auto &id:ids) cJSON_AddItemToArray(list,cJSON_CreateString(id.c_str()));
        auto *select=cJSON_AddArrayToObject(body,"select"); cJSON_AddItemToArray(select,cJSON_CreateString("status"));
        auto *pick=cJSON_AddObjectToObject(body,"pick"),*keys=cJSON_AddArrayToObject(pick,"status");
        for(auto key:{"emeters","_updated","switch:0","switch:1","em1:0","em1:1","em1data:0","em1data:1"}) cJSON_AddItemToArray(keys,cJSON_CreateString(key));
        auto payload=jsonText(body); cJSON_Delete(body);
        auto url=cfg.server+"/v2/devices/api/get?auth_key="+urlEncode(cfg.key);
        if(http.begin(client,url.c_str())) {
            http.addHeader("Content-Type","application/json");
            httpRequests.fetch_add(1);
            if(!client.connected())closedSocketRequests.fetch_add(1);
            auto requestStarted=millis();
            tlsHeapBefore.store(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
            tlsBlockBefore.store(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
            int code=http.POST(reinterpret_cast<uint8_t *>(payload.data()),payload.size());
            lastHttpCode.store(code);
            char tlsMessage[160]{};int tlsCode=code<0?client.lastError(tlsMessage,sizeof(tlsMessage)):0;lastTlsError.store(tlsCode);
            if(code<0)Serial.printf("Cloud HTTPS: HTTP=%d TLS=%d (%s)\n",code,tlsCode,tlsMessage);
            if(code!=200)httpErrors.fetch_add(1);
            if(code==200) {
                LimitedStream response;
                int bytes=http.writeToStream(&response);
                result.ok=bytes>0&&parseCloudReadings(response.data.c_str(),cfg.energy,result.readings);
                if(!result.ok) result.error="Risposta Shelly non valida o troppo grande";
            } else if(code==401||code==403) result.error="Shelly: verifica chiave e server";
            else if(code==429) result.error="Shelly: limite richieste, attendo e riprovo";
            else result.error="Shelly Cloud non raggiungibile (HTTP "+std::to_string(code)+(tlsCode?"; TLS "+std::to_string(tlsCode):"")+")";
            http.end();
            requestDurationMs.store(uint32_t(millis()-requestStarted));
        } else result.error="Impossibile avviare HTTPS Shelly";
        // Never reuse a failed or incompletely consumed response. A successful
        // response may keep its verified TLS socket open when the server allows it.
        if(!result.ok)closeTransport();
        interval=result.ok?3000:std::min(uint32_t(60000),interval*2);
        post(std::move(result));
    }
}
void CloudService::tick(uint32_t now,bool wifiOnline,const RawReadings *local,const DeviceOptions *options) {
    Result result; bool available=false;
    xSemaphoreTake(mutex,portMAX_DELAY);
    if(haveResult) {result=pending;haveResult=false;available=true;}
    xSemaphoreGive(mutex);
    if(available) {
        cloudOK=result.ok;lastCloudError=result.ok?"":result.error;
        if(result.ok){cloudRaw=result.readings;for(auto &r:cloudRaw){r.receivedAt=now;r.maxAgeMs=live.config.staleSeconds*1000;}}
        else live.transport=result.error;
    }
    if(!local||!options){
        if(available){if(result.ok)live.ingest(cloudRaw,now);else live.fail(result.error,now);}
        if(!wifiOnline)live.fail("Wi-Fi non collegato",now);live.tick(now);return;
    }
    auto blend=combineReadings(*local,cloudRaw,live.config,*options,cloudOK,now);
    const auto &merged=blend.values;auto assigned=blend.assigned,lanCount=blend.localCount,cloudCount=blend.cloudCount;
    cloudNeeded.store(blend.needCloud);
    if(wifiOnline){live.ingest(merged,now);
        if(lanCount==assigned&&assigned)live.transport="Rete locale collegata";
        else if(lanCount&&cloudCount)live.transport="Locale + Shelly Cloud";
        else if(cloudCount)live.transport="Shelly Cloud collegato";
        else live.transport=!lastCloudError.empty()?lastCloudError:options->localEnabled?"Cerco i misuratori nella rete locale…":"Collegamento a Shelly Cloud…";
    }else live.fail("Wi-Fi non collegato",now);
}
void CloudService::serialInput() {
    unsigned budget=512;
    while(Serial.available()&&budget--) {
        char c=char(Serial.read()); if(c=='\r') continue;
        if(c!='\n') {if(serialBuffer.size()<8192&&!serialOverflow)serialBuffer+=c;else serialOverflow=true;continue;}
        if(serialOverflow) {serialBuffer.clear();serialOverflow=false;continue;}
        auto *root=cJSON_Parse(serialBuffer.c_str());
        auto nonce=str(get(root,"request_id"));
        bool validNonce=nonce.size()==16&&std::all_of(nonce.begin(),nonce.end(),[](char v){return(v>='0'&&v<='9')||(v>='a'&&v<='f');});
        bool command=cJSON_IsNumber(get(root,"casa_provision"))&&get(root,"casa_provision")->valuedouble==1;
        bool statusCommand=cJSON_IsNumber(get(root,"casa_status"))&&get(root,"casa_status")->valuedouble==1;
        bool displayCommand=cJSON_IsNumber(get(root,"casa_display_test"))&&get(root,"casa_display_test")->valuedouble==1;
        auto *timeoutValue=get(root,"timeout_s");double timeout=cJSON_IsNumber(timeoutValue)?timeoutValue->valuedouble:-1;
        bool timeoutValid=std::isfinite(timeout)&&timeout==std::floor(timeout)&&(timeout==0||(timeout>=15&&timeout<=120));
        cJSON_Delete(root);
        if(displayCommand&&validNonce){bool ok=timeoutValid&&testDisplayTimeout(uint32_t(timeout));Serial.printf("CASA_ACK %s %s\n",nonce.c_str(),ok?"OK":"INVALID_TIMEOUT");}
        else if(command&&validNonce) {
            bool ok=configure(serialBuffer.c_str(),true);
            Serial.printf("CASA_ACK %s %s\n",nonce.c_str(),ok?"OK":"INVALID_CONFIG");
        } else if(statusCommand&&validNonce) {
            auto *state=cJSON_CreateObject();
            cJSON_AddStringToObject(state,"request_id",nonce.c_str());cJSON_AddBoolToObject(state,"configured",ready);
            cJSON_AddBoolToObject(state,"complete",live.complete);cJSON_AddBoolToObject(state,"alarm",live.alarm);
            cJSON_AddStringToObject(state,"connection",live.transport.c_str());
            cJSON_AddStringToObject(state,"firmware_version",FIRMWARE_VERSION);
            auto display=displayStatus();
            cJSON_AddBoolToObject(state,"display_asleep",display.asleep);
            cJSON_AddBoolToObject(state,"display_backlight_on",display.backlightOn);
            cJSON_AddBoolToObject(state,"display_inhibited",display.inhibited);
            cJSON_AddBoolToObject(state,"display_missing_data",display.missing);
            cJSON_AddBoolToObject(state,"display_test_timeout",display.testTimeout);
            cJSON_AddNumberToObject(state,"display_idle_s",display.idleMs/1000);
            cJSON_AddNumberToObject(state,"display_timeout_s",display.timeoutSeconds);
            cJSON_AddNumberToObject(state,"display_saved_timeout_s",display.savedTimeoutSeconds);
            cJSON_AddNumberToObject(state,"display_touch_presses",display.touchPresses);
            cJSON_AddNumberToObject(state,"local_stack_free_min",localStackFree);
            cJSON_AddNumberToObject(state,"cloud_stack_free_min",workerHandle?uxTaskGetStackHighWaterMark(workerHandle):0);
            cJSON_AddNumberToObject(state,"uptime_s",millis()/1000);
            cJSON_AddNumberToObject(state,"free_heap",ESP.getFreeHeap());
            cJSON_AddNumberToObject(state,"min_free_heap",ESP.getMinFreeHeap());
            cJSON_AddNumberToObject(state,"largest_internal_block",heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
            cJSON_AddNumberToObject(state,"free_psram",ESP.getFreePsram());
            cJSON_AddNumberToObject(state,"http_requests",httpRequests.load());
            cJSON_AddNumberToObject(state,"closed_socket_requests",closedSocketRequests.load());
            cJSON_AddNumberToObject(state,"http_errors",httpErrors.load());
            cJSON_AddNumberToObject(state,"last_http_code",lastHttpCode.load());
            cJSON_AddNumberToObject(state,"last_tls_error",lastTlsError.load());
            cJSON_AddNumberToObject(state,"tls_heap_before",tlsHeapBefore.load());
            cJSON_AddNumberToObject(state,"tls_largest_block_before",tlsBlockBefore.load());
            auto tlsStats=tlsMemoryStats();cJSON_AddBoolToObject(state,"tls_psram_policy",tlsStats.installed);
            cJSON_AddNumberToObject(state,"tls_psram_allocations",tlsStats.externalAllocations);
            cJSON_AddNumberToObject(state,"tls_psram_failures",tlsStats.externalFailures);
            cJSON_AddBoolToObject(state,"wifi_connected",WiFi.status()==WL_CONNECTED);
            cJSON_AddStringToObject(state,"local_ip",WiFi.localIP().toString().c_str());
            cJSON_AddNumberToObject(state,"request_duration_ms",requestDurationMs.load());
            if(live.complete)cJSON_AddNumberToObject(state,"total_w",live.total);else cJSON_AddNullToObject(state,"total_w");
            auto *meters=cJSON_AddArrayToObject(state,"circuits");
            for(size_t i=0;i<4;++i) {
                auto *meter=cJSON_CreateObject();const auto &r=live.readings[i];
                cJSON_AddStringToObject(meter,"name",live.config.sources[i].name.c_str());
                cJSON_AddStringToObject(meter,"status",r.reason.c_str());
                cJSON_AddStringToObject(meter,"source",r.local?"local":"cloud");
                cJSON_AddNumberToObject(meter,"age_s",r.age);
                if(r.available)cJSON_AddNumberToObject(meter,"power_w",r.power);else cJSON_AddNullToObject(meter,"power_w");
                cJSON_AddItemToArray(meters,meter);
            }
            auto response=jsonText(state);cJSON_Delete(state);
            Serial.printf("CASA_STATUS %s\n",response.c_str());
        }
        serialBuffer.clear();
    }
}
}
