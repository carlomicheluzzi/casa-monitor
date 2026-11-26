#pragma once
#include "energy_model.h"
#include <Arduino.h>
#include <freertos/semphr.h>
#include <atomic>
#include "device_model.h"

namespace casa {
class UpdateService;
class CloudService {
public:
    EnergyModel live;
    std::vector<MeterSource> catalog;
    void begin();
    void tick(uint32_t now,bool wifiOnline,const RawReadings *local=nullptr,const DeviceOptions *options=nullptr);
    bool schedule(void (*job)(void *),void *context);
    void pause(bool value){pauseRequested.store(value);}
    bool paused()const{return pauseConfirmed.load();}
    uint32_t revision()const{return generation;}
    void mergeCatalog(const std::vector<MeterSource> &sources);
    bool saveConfig(const EnergyConfig &config);
    bool configured() const { return ready; }
    void serialInput();
    void attachUpdates(DeviceOptions &options,UpdateService &service){deviceOptions=&options;updateService=&service;}
    uint32_t localStackFree=0; // diagnostic, written/read only by loop
    std::string serverName() const;
private:
    struct Settings { std::string server,key; EnergyConfig energy; std::vector<MeterSource> catalog; } settings;
    struct Result { bool ok=false; RawReadings readings{}; std::string error; uint32_t generation=0; } pending;
    DeviceOptions *deviceOptions=nullptr;UpdateService *updateService=nullptr;
    TaskHandle_t workerHandle=nullptr;bool workerReady=false;
    SemaphoreHandle_t mutex=nullptr;
    bool ready=false,haveResult=false;
    uint32_t generation=0;
    std::string serialBuffer;
    bool serialOverflow=false;
    std::atomic<uint32_t> httpRequests{0},closedSocketRequests{0},httpErrors{0},requestDurationMs{0};
    std::atomic<int> lastHttpCode{0},lastTlsError{0};
    std::atomic<uint32_t> tlsHeapBefore{0},tlsBlockBefore{0};
    std::string lastCloudError;
    std::atomic<bool> cloudNeeded{true},pauseRequested{false},pauseConfirmed{false};
    void (*job)(void *)=nullptr;void *jobContext=nullptr;bool jobBusy=false;
    RawReadings cloudRaw{};bool cloudOK=false;
    static void taskEntry(void *self);
    void task();
    bool configure(const char *text,bool persist);
    std::string serialize(const Settings &value);
    void post(Result result);
};
}
