#pragma once
#include "energy_model.h"
#include <string>
#include <vector>

namespace casa {
struct LanDevice {std::string id,host,user="admin",password;int generation=0;};
struct DeviceOptions {
    uint32_t screenTimeout=120;unsigned brightness=70;bool wakeMissing=true;
    bool localEnabled=true,cloudFallback=true;uint32_t pollMs=1000,localStaleMs=5000;
    std::vector<LanDevice> devices;
    std::string manifestUrl;bool dailyCheck=true,autoInstall=false;
};
bool localHostAllowed(const std::string &host);
bool httpsUrlAllowed(const std::string &url,size_t maxLength=512);
std::string normalizedDeviceId(const std::string &value);
std::string validateOptions(const DeviceOptions &options);
bool parseOptions(const char *text,DeviceOptions &options);
std::string encodeOptions(const DeviceOptions &options);
bool parseLocalIdentity(const char *text,const std::string &expected,int &generation,std::string &model);
bool parseLocalStatus(const char *text,const std::string &id,int generation,const EnergyConfig &config,uint32_t now,uint32_t maxAge,RawReadings &out);
struct CombinedReadings {RawReadings values{};bool needCloud=false;unsigned localCount=0,cloudCount=0,assigned=0;};
CombinedReadings combineReadings(const RawReadings &local,const RawReadings &cloud,const EnergyConfig &energy,const DeviceOptions &options,bool cloudOK,uint32_t now);
struct LocalDeviceView {LanDevice endpoint;std::string model,status;};
class ScreenPolicy {
public:
    bool asleep=false;
    void configure(const DeviceOptions &options,uint32_t now);
    void tick(uint32_t now,bool alarm,bool missing,bool inhibit=false);
    bool touch(bool pressed,uint32_t now); // false consumes the entire wake gesture
    void wake(uint32_t now);
    void setTimeout(uint32_t seconds,uint32_t now){settings.screenTimeout=seconds;wake(now);}
    uint32_t timeout()const{return settings.screenTimeout;}
    uint32_t idle(uint32_t now)const{return uint32_t(now-activeAt);}
private:
    DeviceOptions settings;uint32_t activeAt=0,missingAt=0;bool missingSeen=false,missingAlerted=false,consume=false;
};
struct UpdateView {bool busy=false,available=false,maintenance=false,signatureVerified=false,manifestCompatible=false;unsigned progress=0,checks=0,redirects=0;int httpCode=0,tlsError=0;uint32_t checkedAt=0;std::string status="Aggiornamenti da configurare",version,notes;};
struct ReleaseInfo {unsigned code=0;std::string version,url,sha256,notes;size_t size=0;};
bool parseRelease(const char *payload,unsigned currentCode,size_t slotSize,ReleaseInfo &release,std::string &error);
}
