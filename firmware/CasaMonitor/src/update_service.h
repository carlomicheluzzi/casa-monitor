#pragma once
#include "device_model.h"
#include "cloud_service.h"
#include <atomic>
namespace casa {
class UpdateService {
public:
    void begin(CloudService &cloud);
    void configure(const DeviceOptions &options);
    void check(){checkRequested.store(true);}
    void install(){installRequested.store(true);}
    void tick(bool safe,bool quiet); // called by loop; safe excludes demo/missing/alarm
    UpdateView snapshot();
    bool maintenance()const{return dark.load();}
private:
    CloudService *cloud=nullptr;SemaphoreHandle_t mutex=nullptr;
    DeviceOptions options;UpdateView view;ReleaseInfo release;
    std::atomic<bool> checkRequested{false},installRequested{false},safeNow{false},dark{false},bootReady{false};
    uint32_t generation=0,bootAt=0,lastCheck=0,quietAt=0,lastInstall=0;unsigned blockedCode=0;bool pendingBoot=false,attempted=false,wasQuiet=false,installAttempted=false;
    std::atomic<uint32_t> retry{86400000};std::atomic<bool> scheduledInstall{false};
    static void entry(void *self);void task();
    void status(const std::string &text,bool busy=false);
    bool fetch(const std::string &url,std::string &body);
    bool apply(const ReleaseInfo &release);
};
}
