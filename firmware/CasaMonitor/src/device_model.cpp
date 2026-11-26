#include "device_model.h"
#include <cJSON.h>
#include <algorithm>
#include <cmath>
#include <set>
#include <cstdio>
#include <cctype>

namespace casa {
namespace {
const cJSON *get(const cJSON *v,const char *k){return cJSON_GetObjectItemCaseSensitive(v,k);}
std::string str(const cJSON *v){return cJSON_IsString(v)&&v->valuestring?v->valuestring:"";}
double num(const cJSON *v){return cJSON_IsNumber(v)&&std::isfinite(v->valuedouble)?v->valuedouble:MISSING;}
bool integer(const cJSON *v,double lo,double hi){double n=num(v);return std::isfinite(n)&&n>=lo&&n<=hi&&n==std::floor(n);}
bool printable(const std::string &s,size_t max){return s.size()<=max&&std::none_of(s.begin(),s.end(),[](unsigned char c){return c<32||c==127;});}
std::string json(cJSON *v){char *p=cJSON_PrintUnformatted(v);std::string s=p?p:"";cJSON_free(p);return s;}
}
std::string normalizedDeviceId(const std::string &s){
    std::string out;for(unsigned char c:s)if(c!=':'&&c!='-')out+=char(std::tolower(c));
    if(out.size()>12)out=out.substr(out.size()-12);
    if(out.size()!=12||!std::all_of(out.begin(),out.end(),[](char c){return(c>='0'&&c<='9')||(c>='a'&&c<='f');}))return "";
    return out;
}
bool localHostAllowed(const std::string &s){
    if(s.empty())return true;
    if(s.size()>64)return false;
    unsigned a,b,c,d;int count=0;
    if(std::sscanf(s.c_str(),"%u.%u.%u.%u%n",&a,&b,&c,&d,&count)==4&&count==int(s.size())&&a<256&&b<256&&c<256&&d<256)
        return (a==10||(a==172&&b>=16&&b<=31)||(a==192&&b==168))&&d!=0&&d!=255;
    return s.size()>6&&s.compare(s.size()-6,6,".local")==0&&s.front()!='.'&&
        std::all_of(s.begin(),s.end(),[](char c){return(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'||c=='.';});
}
bool httpsUrlAllowed(const std::string &s,size_t maxLength){
    if(s.rfind("https://",0)!=0||s.size()>maxLength||!printable(s,maxLength)||s.find_first_of(" @\\#\r\n\t")!=std::string::npos)return false;
    auto end=s.find('/',8);auto host=s.substr(8,end==std::string::npos?s.size()-8:end-8);
    return !host.empty()&&std::all_of(host.begin(),host.end(),[](char c){return(c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='.';});
}
std::string validateOptions(const DeviceOptions &c){
    if(c.screenTimeout>3600||(c.screenTimeout&&c.screenTimeout<15)||c.brightness<5||c.brightness>100)return "Standby: 15–3600 s oppure Mai. Luminosità: 5–100%.";
    if(c.pollMs<1000||c.pollMs>10000||c.localStaleMs<3000||c.localStaleMs>30000||c.localStaleMs<c.pollMs*2)return "Controlla intervallo locale e scadenza dei dati.";
    if(!c.manifestUrl.empty()&&!httpsUrlAllowed(c.manifestUrl))return "L’indirizzo aggiornamenti deve essere HTTPS, senza credenziali.";
    if(!c.localEnabled&&!c.cloudFallback)return "Attiva almeno una sorgente: rete locale o cloud.";
    if(c.devices.size()>7)return "Sono supportati al massimo sette dispositivi locali.";
    std::set<std::string> ids;
    for(const auto &d:c.devices)if(d.id.empty()||normalizedDeviceId(d.id)!=d.id||!ids.insert(d.id).second||!localHostAllowed(d.host)||!printable(d.user,32)||!printable(d.password,64)||d.generation<0||d.generation>4)return "Identificativo, host o credenziali locali non validi.";
    return "";
}
std::string encodeOptions(const DeviceOptions &c){
    auto *r=cJSON_CreateObject();
    cJSON_AddNumberToObject(r,"screen_timeout",c.screenTimeout);cJSON_AddNumberToObject(r,"brightness",c.brightness);cJSON_AddBoolToObject(r,"wake_missing",c.wakeMissing);
    cJSON_AddBoolToObject(r,"local_enabled",c.localEnabled);cJSON_AddBoolToObject(r,"cloud_fallback",c.cloudFallback);
    cJSON_AddNumberToObject(r,"poll_ms",c.pollMs);cJSON_AddNumberToObject(r,"local_stale_ms",c.localStaleMs);
    cJSON_AddStringToObject(r,"manifest_url",c.manifestUrl.c_str());cJSON_AddBoolToObject(r,"daily_check",c.dailyCheck);cJSON_AddBoolToObject(r,"auto_install",c.autoInstall);
    auto *list=cJSON_AddArrayToObject(r,"devices");
    for(const auto &d:c.devices){auto *v=cJSON_CreateObject();cJSON_AddStringToObject(v,"id",d.id.c_str());cJSON_AddStringToObject(v,"host",d.host.c_str());cJSON_AddStringToObject(v,"user",d.user.c_str());cJSON_AddStringToObject(v,"password",d.password.c_str());cJSON_AddNumberToObject(v,"generation",d.generation);cJSON_AddItemToArray(list,v);}
    auto result=json(r);cJSON_Delete(r);return result;
}
bool parseOptions(const char *text,DeviceOptions &out){
    auto *r=cJSON_ParseWithOpts(text,nullptr,true);if(!cJSON_IsObject(r)){cJSON_Delete(r);return false;}DeviceOptions c;
    bool ok=integer(get(r,"screen_timeout"),0,3600)&&integer(get(r,"brightness"),5,100)&&integer(get(r,"poll_ms"),1000,10000)&&integer(get(r,"local_stale_ms"),3000,30000);
    for(auto key:{"wake_missing","local_enabled","cloud_fallback","daily_check","auto_install"})ok=ok&&cJSON_IsBool(get(r,key));
    ok=ok&&cJSON_IsString(get(r,"manifest_url"));
    if(!ok){cJSON_Delete(r);return false;}
    c.screenTimeout=uint32_t(num(get(r,"screen_timeout")));c.brightness=unsigned(num(get(r,"brightness")));
    c.pollMs=uint32_t(num(get(r,"poll_ms")));c.localStaleMs=uint32_t(num(get(r,"local_stale_ms")));
    c.wakeMissing=cJSON_IsTrue(get(r,"wake_missing"));c.localEnabled=cJSON_IsTrue(get(r,"local_enabled"));c.cloudFallback=cJSON_IsTrue(get(r,"cloud_fallback"));
    c.dailyCheck=cJSON_IsTrue(get(r,"daily_check"));c.autoInstall=cJSON_IsTrue(get(r,"auto_install"));c.manifestUrl=str(get(r,"manifest_url"));
    auto *list=get(r,"devices");ok=ok&&cJSON_IsArray(list)&&cJSON_GetArraySize(list)<=7;
    if(ok)for(auto *v=list->child;v;v=v->next){if(!cJSON_IsObject(v)||!cJSON_IsString(get(v,"id"))||!cJSON_IsString(get(v,"host"))||!cJSON_IsString(get(v,"user"))||!cJSON_IsString(get(v,"password"))||!integer(get(v,"generation"),0,4)){ok=false;break;}c.devices.push_back({str(get(v,"id")),str(get(v,"host")),str(get(v,"user")),str(get(v,"password")),int(num(get(v,"generation")))});}
    cJSON_Delete(r);if(!ok||!validateOptions(c).empty())return false;out=c;return true;
}
bool parseLocalIdentity(const char *text,const std::string &expected,int &generation,std::string &model){
    auto *r=cJSON_Parse(text);if(!r)return false;
    auto id=normalizedDeviceId(str(get(r,"mac")));if(id.empty())id=normalizedDeviceId(str(get(r,"id")));
    generation=integer(get(r,"gen"),2,4)?int(num(get(r,"gen"))):1;
    model=str(get(r,"model"));if(model.empty())model=str(get(r,"type"));
    bool ok=!id.empty()&&(expected.empty()||id==expected);cJSON_Delete(r);return ok;
}
bool parseLocalStatus(const char *text,const std::string &id,int generation,const EnergyConfig &c,uint32_t now,uint32_t maxAge,RawReadings &out){
    auto *r=cJSON_Parse(text);if(!cJSON_IsObject(r)){cJSON_Delete(r);return false;}
    if(cJSON_IsObject(get(r,"result"))){auto *copy=cJSON_Duplicate(get(r,"result"),true);cJSON_Delete(r);r=copy;}
    bool any=false;
    for(size_t i=0;i<METER_COUNT;++i)if(c.sources[i].device==id){auto &v=out[i];auto source=c.sources[i].source;const cJSON *meter=nullptr;
        v.online=true;v.local=true;v.receivedAt=now;v.maxAgeMs=maxAge;
        if(generation==1&&source.rfind("emeters[",0)==0){meter=cJSON_GetArrayItem(get(r,"emeters"),source=="emeters[1]"?1:0);v.power=num(get(meter,"power"));v.energyWh=num(get(meter,"total"));v.valid=cJSON_IsTrue(get(meter,"is_valid"))&&std::isfinite(v.power);}
        else if(source.rfind("switch:",0)==0){meter=get(r,source.c_str());v.power=num(get(meter,"apower"));v.energyWh=num(get(get(meter,"aenergy"),"total"));v.valid=std::isfinite(v.power)&&cJSON_GetArraySize(get(meter,"errors"))==0;}
        else if(source.rfind("em1:",0)==0){meter=get(r,source.c_str());v.power=num(get(meter,"act_power"));v.energyWh=num(get(get(r,("em1data:"+source.substr(4)).c_str()),"total_act_energy"));v.valid=std::isfinite(v.power)&&cJSON_GetArraySize(get(meter,"errors"))==0;}
        v.voltage=num(get(meter,"voltage"));if(v.energyWh<0)v.energyWh=MISSING;any=any||v.valid;
    }
    cJSON_Delete(r);return any;
}
CombinedReadings combineReadings(const RawReadings &local,const RawReadings &cloud,const EnergyConfig &energy,const DeviceOptions &options,bool cloudOK,uint32_t now){
    CombinedReadings result;
    for(size_t i=0;i<METER_COUNT;++i){
        if(energy.sources[i].device.empty())continue;++result.assigned;
        const auto &reading=local[i];
        bool fresh=options.localEnabled&&reading.local&&reading.valid&&reading.online&&uint32_t(now-reading.receivedAt)<=options.localStaleMs;
        if(fresh){result.values[i]=reading;++result.localCount;}
        else {
            if(!options.localEnabled||options.cloudFallback)result.needCloud=true;
            if((!options.localEnabled||options.cloudFallback)&&cloudOK){result.values[i]=cloud[i];++result.cloudCount;}
            else {result.values[i]=reading;result.values[i].local=options.localEnabled;result.values[i].valid=false;}
        }
    }return result;
}
void ScreenPolicy::configure(const DeviceOptions &c,uint32_t now){settings=c;missingSeen=false;missingAlerted=false;wake(now);}
void ScreenPolicy::wake(uint32_t now){asleep=false;activeAt=now;}
void ScreenPolicy::tick(uint32_t now,bool alarm,bool missing,bool inhibit){
    if(missing&&!missingSeen){missingAt=now;missingSeen=true;missingAlerted=false;}
    if(!missing){missingSeen=false;missingAlerted=false;}
    if(alarm||inhibit){wake(now);return;}
    // Notify once per outage. Repeated stale samples must not restart the idle
    // timer forever; only a real alarm deliberately keeps the display awake.
    if(settings.wakeMissing&&missingSeen&&!missingAlerted&&uint32_t(now-missingAt)>=10000){missingAlerted=true;wake(now);return;}
    if(settings.screenTimeout&&uint32_t(now-activeAt)>=settings.screenTimeout*1000)asleep=true;
}
bool ScreenPolicy::touch(bool pressed,uint32_t now){
    if(pressed){if(asleep)consume=true;wake(now);return !consume;}
    bool accepted=!consume;consume=false;return accepted;
}
bool parseRelease(const char *text,unsigned current,size_t slot,ReleaseInfo &out,std::string &error){
    auto *r=cJSON_ParseWithOpts(text,nullptr,true);ReleaseInfo v;
    bool ok=cJSON_IsObject(r)&&integer(get(r,"schema"),1,1)&&str(get(r,"board"))=="waveshare-esp32-s3-touch-lcd-4.3c"&&integer(get(r,"version_code"),1,4294967295.0)&&integer(get(r,"size"),1024,double(slot));
    if(ok){v.code=unsigned(num(get(r,"version_code")));v.size=size_t(num(get(r,"size")));v.version=str(get(r,"version"));v.url=str(get(r,"url"));v.sha256=str(get(r,"sha256"));v.notes=str(get(r,"notes"));
        ok=v.code>current&&printable(v.version,40)&&!v.version.empty()&&httpsUrlAllowed(v.url)&&v.sha256.size()==64&&std::all_of(v.sha256.begin(),v.sha256.end(),[](char c){return(c>='0'&&c<='9')||(c>='a'&&c<='f');})&&printable(v.notes,500);}
    cJSON_Delete(r);if(!ok){error="Aggiornamento non compatibile, non più recente o manifest non valido.";return false;}out=v;return true;
}
}
