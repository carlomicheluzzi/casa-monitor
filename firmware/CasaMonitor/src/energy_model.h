#pragma once
#include <array>
#include <string>
#include <vector>
#include <cstdint>
#include <limits>

namespace casa {
constexpr double MISSING=std::numeric_limits<double>::quiet_NaN();
constexpr size_t METER_COUNT=7;
struct MeterSource { std::string name,device,source; int zone=-1; };
struct EnergyConfig {
    double limit=4500, warning=4000, hysteresis=200;
    uint32_t staleSeconds=90, muteSeconds=120;
    bool mappingConfirmed=false, audioEnabled=true;
    std::array<MeterSource,METER_COUNT> sources{{
        {"Zona 1 · Luce","485519d9cf2f","emeters[1]",0},
        {"Zona 2 · Luce","485519d9502e","emeters[1]",1},
        {"Zona 1 · Forza","485519d9cf2f","emeters[0]",0},
        {"Zona 2 · Forza","485519d9502e","emeters[0]",1},
        {"Boiler Carlo","","",-1}, {"Boiler Gloria","","",-1},
        {"Lavatrice + asciugatrice","","",-1}
    }};
};
struct RawMeter {
    bool online=false, valid=false;
    double power=MISSING,voltage=MISSING,energyWh=MISSING;
    std::string marker;
    bool local=false;
    uint32_t receivedAt=0,maxAgeMs=0;
};
using RawReadings=std::array<RawMeter,METER_COUNT>;
struct MeterReading {
    bool assigned=false, available=false, verified=false;
    double power=MISSING,voltage=MISSING,energyWh=MISSING;
    uint32_t age=0;
    std::string reason="In attesa",marker;
    bool local=false;
};
std::string validateEnergyConfig(const EnergyConfig &config);
bool parseEnergyConfig(const char *json,EnergyConfig &config);
std::string encodeEnergyConfig(const EnergyConfig &config);
bool parseCloudReadings(const char *json,const EnergyConfig &config,RawReadings &readings);

class EnergyModel {
public:
    EnergyConfig config;
    std::array<MeterReading,METER_COUNT> readings{};
    bool complete=false,alarm=false,audible=false;
    double total=MISSING;
    uint32_t mutedSeconds=0;
    std::string transport="Collegamento a Shelly Cloud…";
    void configure(const EnergyConfig &value);
    void ingest(const RawReadings &value,uint32_t now);
    void fail(const std::string &message,uint32_t now);
    void tick(uint32_t now);
    void mute(uint32_t now);
    void simulate(int scenario,uint32_t now);
    std::vector<float> history(size_t circuit) const;
private:
    RawReadings latest{};
    std::array<uint32_t,METER_COUNT> changedAt{};
    bool transportOK=false,hasPoll=false,muted=false;
    uint32_t lastPoll=0,muteStart=0,lastHistory=0;
    std::array<std::array<float,4>,300> samples{};
    size_t sampleCount=0,sampleHead=0;
};
}
