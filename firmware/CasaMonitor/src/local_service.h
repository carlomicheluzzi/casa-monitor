#pragma once
#include "device_model.h"
#include <Arduino.h>
#include <freertos/semphr.h>
#include <atomic>
namespace casa {
class LocalService {
public:
    void begin();
    uint32_t freeStack()const{return workerHandle?uxTaskGetStackHighWaterMark(workerHandle):0;}
    void configure(const DeviceOptions &options,const EnergyConfig &energy);
    void discover(){scanRequested.store(true);}
    void snapshot(RawReadings &readings,std::vector<LocalDeviceView> &devices,std::vector<MeterSource> &catalog);
private:
    TaskHandle_t workerHandle=nullptr;
    SemaphoreHandle_t mutex=nullptr;
    DeviceOptions options;EnergyConfig energy;uint32_t generation=0;
    RawReadings readings{};std::vector<LocalDeviceView> views;std::vector<MeterSource> catalog;
    std::atomic<bool> scanRequested{true};
    static void entry(void *self);
    void task();
};
}
