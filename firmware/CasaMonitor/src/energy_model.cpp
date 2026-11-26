#include "energy_model.h"
#include "cJSON.h"
#include <cmath>
#include <set>
#include <algorithm>

namespace casa {
namespace {
const cJSON *get(const cJSON *v,const char *key) { return cJSON_GetObjectItemCaseSensitive(v,key); }
std::string string(const cJSON *v) { return cJSON_IsString(v)&&v->valuestring?v->valuestring:""; }
double number(const cJSON *v) { return cJSON_IsNumber(v)&&std::isfinite(v->valuedouble)?v->valuedouble:MISSING; }
bool safeName(const std::string &s) {
    return !s.empty() && s.size()<=64 && std::none_of(s.begin(),s.end(),[](unsigned char v){return v<32;});
}
bool deviceId(const std::string &s) {
    return s.size()==12 && std::all_of(s.begin(),s.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});
}
bool sourceId(const std::string &s) {
    return s=="emeters[0]"||s=="emeters[1]"||s=="switch:0"||s=="switch:1"||s=="em1:0"||s=="em1:1";
}
}
std::string validateEnergyConfig(const EnergyConfig &c) {
    if(!std::isfinite(c.limit)||c.limit<500||c.limit>30000 ||
       !std::isfinite(c.warning)||c.warning<100||c.warning>=c.limit ||
       !std::isfinite(c.hysteresis)||c.hysteresis<10||c.hysteresis>2000||c.hysteresis>=c.limit)
        return "Controlla soglia, preavviso e margine di rientro.";
    if(c.staleSeconds<15||c.staleSeconds>300||c.muteSeconds<10||c.muteSeconds>900)
        return "Durata dati: 15–300 s. Silenziamento: 10–900 s.";
    std::set<std::string> identities;
    for(size_t i=0;i<c.sources.size();++i) {
        const auto &s=c.sources[i];
        if(!safeName(s.name)) return "Nome del circuito non valido.";
        if(i>=4 && s.device.empty() && s.source.empty()) continue;
        if(!deviceId(s.device)||!sourceId(s.source)||s.zone<0||s.zone>1)
            return "Scegli misuratore, canale e zona validi.";
        if(i<4 && s.source.rfind("emeters[",0)!=0) return "I quattro circuiti richiedono i canali Shelly EM.";
        if(!identities.insert(s.device+"/"+s.source).second)
            return "Lo stesso canale non può essere contato o assegnato due volte.";
    }
    return "";
}
bool parseEnergyConfig(const char *text,EnergyConfig &config) {
    cJSON *root=cJSON_Parse(text);
    if(!root) return false;
    EnergyConfig c;
    c.limit=number(get(root,"limit_w")); c.warning=number(get(root,"warning_w"));
    c.hysteresis=number(get(root,"hysteresis_w"));
    double stale=number(get(root,"stale_after_s")),mute=number(get(root,"mute_seconds"));
    const auto *confirmed=get(root,"mapping_confirmed"),*audio=get(root,"audio_enabled");
    bool valid=std::isfinite(stale)&&stale>=15&&stale<=300&&std::floor(stale)==stale &&
               std::isfinite(mute)&&mute>=10&&mute<=900&&std::floor(mute)==mute&&
               cJSON_IsBool(confirmed)&&cJSON_IsBool(audio);
    if(valid) { c.staleSeconds=uint32_t(stale); c.muteSeconds=uint32_t(mute); }
    c.mappingConfirmed=cJSON_IsTrue(confirmed); c.audioEnabled=cJSON_IsTrue(audio);
    const auto *sources=get(root,"sources");
    valid=valid && cJSON_IsArray(sources)&&cJSON_GetArraySize(sources)==int(METER_COUNT);
    if(valid) for(size_t i=0;i<METER_COUNT;++i) {
        const auto *s=cJSON_GetArrayItem(sources,int(i));
        double zone=number(get(s,"zone"));
        if(!std::isfinite(zone)||zone<-1||zone>1||std::floor(zone)!=zone) { valid=false; break; }
        c.sources[i]={string(get(s,"name")),string(get(s,"device")),string(get(s,"source")),int(zone)};
    }
    cJSON_Delete(root);
    if(!valid || !validateEnergyConfig(c).empty()) return false;
    config=c; return true;
}
std::string encodeEnergyConfig(const EnergyConfig &c) {
    auto *root=cJSON_CreateObject();
    cJSON_AddNumberToObject(root,"limit_w",c.limit); cJSON_AddNumberToObject(root,"warning_w",c.warning);
    cJSON_AddNumberToObject(root,"hysteresis_w",c.hysteresis);
    cJSON_AddNumberToObject(root,"stale_after_s",c.staleSeconds); cJSON_AddNumberToObject(root,"mute_seconds",c.muteSeconds);
    cJSON_AddBoolToObject(root,"mapping_confirmed",c.mappingConfirmed); cJSON_AddBoolToObject(root,"audio_enabled",c.audioEnabled);
    auto *array=cJSON_AddArrayToObject(root,"sources");
    for(const auto &s:c.sources) {
        auto *v=cJSON_CreateObject();
        cJSON_AddStringToObject(v,"name",s.name.c_str()); cJSON_AddStringToObject(v,"device",s.device.c_str());
        cJSON_AddStringToObject(v,"source",s.source.c_str()); cJSON_AddNumberToObject(v,"zone",s.zone);
        cJSON_AddItemToArray(array,v);
    }
    char *text=cJSON_PrintUnformatted(root); std::string result=text?text:"";
    cJSON_free(text); cJSON_Delete(root); return result;
}
bool parseCloudReadings(const char *text,const EnergyConfig &config,RawReadings &out) {
    cJSON *root=cJSON_Parse(text);
    if(!cJSON_IsArray(root)) { cJSON_Delete(root); return false; }
    RawReadings result{};
    for(size_t i=0;i<METER_COUNT;++i) {
        const auto &source=config.sources[i];
        const cJSON *device=nullptr;
        for(auto *d=root->child;d;d=d->next) if(string(get(d,"id"))==source.device) {device=d;break;}
        if(!device) continue;
        auto &r=result[i]; const auto *online=get(device,"online");
        r.online=cJSON_IsTrue(online)||(cJSON_IsNumber(online)&&online->valuedouble==1);
        const auto *s=get(device,"status"); r.marker=string(get(s,"_updated"));
        const cJSON *meter=nullptr;
        if(source.source.rfind("emeters[",0)==0) {
            int ch=source.source=="emeters[1]"?1:0;
            meter=cJSON_GetArrayItem(get(s,"emeters"),ch);
            r.valid=cJSON_IsTrue(get(meter,"is_valid"));
            r.power=number(get(meter,"power")); r.energyWh=number(get(meter,"total"));
        } else if(source.source.rfind("switch:",0)==0) {
            meter=get(s,source.source.c_str());
            r.power=number(get(meter,"apower"));
            r.energyWh=number(get(get(meter,"aenergy"),"total"));
            r.valid=std::isfinite(r.power)&&!cJSON_GetArraySize(get(meter,"errors"));
        } else if(source.source.rfind("em1:",0)==0) {
            meter=get(s,source.source.c_str());
            r.power=number(get(meter,"act_power"));
            auto energyKey="em1data:"+source.source.substr(4);
            auto *energy=get(s,energyKey.c_str());
            r.energyWh=cJSON_GetArraySize(get(energy,"errors"))?MISSING:number(get(energy,"total_act_energy"));
            r.valid=std::isfinite(r.power)&&!cJSON_GetArraySize(get(meter,"errors"));
        }
        r.voltage=number(get(meter,"voltage"));
        if(r.energyWh<0) r.energyWh=MISSING;
    }
    cJSON_Delete(root); out=result; return true;
}
void EnergyModel::configure(const EnergyConfig &value) {
    bool changed=false;
    for(size_t i=0;i<METER_COUNT;++i) if(config.sources[i].device!=value.sources[i].device||config.sources[i].source!=value.sources[i].source) changed=true;
    config=value;
    if(changed) {
        readings={}; latest={}; changedAt={}; sampleCount=sampleHead=0;
        complete=false; total=MISSING; hasPoll=false; transportOK=false;
    }
}
void EnergyModel::ingest(const RawReadings &value,uint32_t now) {
    latest=value; transportOK=hasPoll=true; lastPoll=now; transport="Shelly Cloud collegato";
    for(size_t i=0;i<METER_COUNT;++i) {
        auto &r=readings[i]; const auto &v=value[i];
        if(r.local!=v.local){r.marker.clear();r.verified=false;r.local=v.local;}
        if(v.local){r.verified=v.valid;changedAt[i]=v.receivedAt;}
        else if(!v.marker.empty() && v.marker!=r.marker) {
            r.verified=!r.marker.empty() && v.marker>r.marker;
            r.marker=v.marker; changedAt[i]=now;
        }
        if(std::isfinite(v.power)) r.power=v.power;
        r.voltage=v.voltage; r.energyWh=v.energyWh;
    }
    tick(now);
}
void EnergyModel::fail(const std::string &message,uint32_t now) { transportOK=false; transport=message; tick(now); }
void EnergyModel::tick(uint32_t now) {
    complete=true; double sum=0;
    for(size_t i=0;i<METER_COUNT;++i) {
        auto &r=readings[i]; const auto &v=latest[i];
        r.assigned=!config.sources[i].device.empty();
        r.age=uint32_t(now-changedAt[i])/1000;
        r.reason=!r.assigned?"Da associare":!transportOK?"Connessione assente":!v.online?"Dispositivo offline":
            !v.valid||!std::isfinite(v.power)?"Misura assente":!r.verified?"Verifico aggiornamento":
            (v.maxAgeMs&&uint32_t(now-v.receivedAt)>v.maxAgeMs)||r.age>config.staleSeconds||!hasPoll||uint32_t(now-lastPoll)>config.staleSeconds*1000?"Dato scaduto":"Aggiornato";
        r.available=r.reason=="Aggiornato";
        if(i<4) { complete=complete&&r.available; sum+=r.power; }
    }
    total=complete?sum:MISSING;
    if(complete) {
        if(total>config.limit && !alarm) { alarm=true; muted=false; }
        else if(alarm && total<=config.limit-config.hysteresis) { alarm=false; muted=false; }
    }
    uint32_t elapsed=uint32_t(now-muteStart);
    if(muted && elapsed>=config.muteSeconds*1000) muted=false;
    mutedSeconds=alarm&&muted?(config.muteSeconds*1000-elapsed+999)/1000:0;
    audible=alarm&&mutedSeconds==0&&config.audioEnabled;
    if(!sampleCount || uint32_t(now-lastHistory)>=3000) {
        for(size_t i=0;i<4;++i) samples[sampleHead][i]=readings[i].available?float(readings[i].power):float(MISSING);
        sampleHead=(sampleHead+1)%samples.size(); sampleCount=std::min(sampleCount+1,samples.size()); lastHistory=now;
    }
}
void EnergyModel::mute(uint32_t now) { if(alarm) {muteStart=now;muted=true;tick(now);} }
std::vector<float> EnergyModel::history(size_t circuit) const {
    std::vector<float> result; if(circuit>=4) return result;
    for(size_t i=0;i<sampleCount;++i) result.push_back(samples[(sampleHead+samples.size()-sampleCount+i)%samples.size()][circuit]);
    return result;
}
void EnergyModel::simulate(int scenario,uint32_t now) {
    if(scenario==3) {
        auto readings=latest;
        for(auto &r:readings){r.online=false;r.valid=false;}
        ingest(readings,now);return;
    }
    RawReadings r{};
    const double values[3][4]={{120,90,650,420},{180,160,2300,1460},{240,180,2750,2100}};
    int mode=std::max(0,std::min(scenario,2));
    for(size_t i=0;i<METER_COUNT;++i) {
        r[i]={scenario!=3,scenario!=3,i<4?values[mode][i]:double(120*(i-3)),230,100000+double(now)/3600000*100,
              std::to_string(10000000000ull+now/3000)};
    }
    for(size_t i=0;i<METER_COUNT;++i) readings[i].marker="0";
    ingest(r,now);
}
}
