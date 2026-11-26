#include "energy_model.h"
#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>

using namespace casa;
#define CHECK(x) do{if(!(x))throw std::runtime_error("Energy test, line "+std::to_string(__LINE__)+": " #x);}while(0)
static RawReadings sample(double power=100,const char *marker="2026-11-13 10:00:00") {
    RawReadings r;
    for(auto &item:r)item={true,true,power,230,12345,marker};
    return r;
}
static void verified(EnergyModel &model,double power=100) {
    model.ingest(sample(power),100);
    model.ingest(sample(power,"2026-11-13 10:00:01"),200);
}
int main() {
    std::vector<std::pair<const char *,std::function<void()>>> cases={
        {"cached first response is not trusted",[]{EnergyModel m;m.ingest(sample(),100);CHECK(!m.complete);CHECK(std::isnan(m.total));}},
        {"advancing markers make four distinct mains valid",[]{EnergyModel m;verified(m);CHECK(m.complete);CHECK(m.total==400);CHECK(m.readings[0].energyWh==12345);}},
        {"HTTP success does not refresh an unchanged cache",[]{EnergyModel m;verified(m);m.ingest(sample(100,"2026-11-13 10:00:01"),92000);CHECK(!m.complete);CHECK(m.readings[0].reason=="Dato scaduto");}},
        {"backward clock requires a subsequent advance",[]{EnergyModel m;verified(m);m.ingest(sample(100,"2026-11-13 09:00:00"),300);CHECK(!m.complete);m.ingest(sample(100,"2026-11-13 09:00:01"),400);CHECK(m.complete);}},
        {"missing or invalid main never becomes zero",[]{EnergyModel m;verified(m);auto r=sample();r[0].valid=false;m.ingest(r,300);CHECK(!m.complete);CHECK(std::isnan(m.total));CHECK(m.readings[0].power==100);}},
        {"strict threshold and hysteresis",[]{EnergyModel m;verified(m,1125);CHECK(!m.alarm);m.ingest(sample(1200,"2026-11-13 10:00:02"),300);CHECK(m.alarm);m.ingest(sample(1100,"2026-11-13 10:00:03"),400);CHECK(m.alarm);m.ingest(sample(1075,"2026-11-13 10:00:04"),500);CHECK(!m.alarm);}},
        {"data loss cannot clear an existing alarm",[]{EnergyModel m;verified(m,1500);m.fail("offline",300);CHECK(m.alarm);CHECK(m.audible);CHECK(!m.complete);}},
        {"mute expires and new episode resets it",[]{EnergyModel m;verified(m,1500);m.mute(300);CHECK(!m.audible);CHECK(m.mutedSeconds==120);m.tick(120300);CHECK(m.audible);m.ingest(sample(100,"2026-11-13 10:00:02"),120400);CHECK(!m.alarm);m.ingest(sample(1500,"2026-11-13 10:00:03"),120500);CHECK(m.audible);}},
        {"audio can be disabled independently from visual alarm",[]{EnergyModel m;m.config.audioEnabled=false;verified(m,1500);CHECK(m.alarm);CHECK(!m.audible);}},
        {"submeters never increase house total",[]{EnergyModel m;m.config.sources[4]={"Boiler","aabbccddeeff","switch:0",0};verified(m);auto r=sample(100,"2026-11-13 10:00:02");r[4].power=5000;m.ingest(r,300);CHECK(m.total==400);CHECK(m.readings[4].power==5000);}},
        {"duplicate or reused mains are rejected",[]{EnergyConfig c;CHECK(validateEnergyConfig(c).empty());c.sources[4]=c.sources[0];CHECK(!validateEnergyConfig(c).empty());c.sources[4]={"Boiler","aabbccddeeff","switch:0",0};c.sources[5]=c.sources[4];CHECK(!validateEnergyConfig(c).empty());}},
        {"mapping change invalidates old readings",[]{EnergyModel m;verified(m);auto c=m.config;std::swap(c.sources[0].device,c.sources[1].device);m.configure(c);m.tick(300);CHECK(!m.complete);}},
        {"JSON configuration roundtrip and bounds",[]{EnergyConfig c;EnergyConfig other;CHECK(parseEnergyConfig(encodeEnergyConfig(c).c_str(),other));CHECK(other.sources[0].source=="emeters[1]");c.staleSeconds=0;CHECK(!parseEnergyConfig(encodeEnergyConfig(c).c_str(),other));CHECK(!parseEnergyConfig("{}",other));}},
        {"simulation and real alarms are isolated",[]{EnergyModel real,demo;verified(real);demo.simulate(2,1000);CHECK(demo.alarm);CHECK(!real.alarm);CHECK(real.total==400);demo.simulate(3,4000);CHECK(demo.alarm);CHECK(!demo.complete);CHECK(real.complete);}},
        {"history records gaps rather than fictitious zeros",[]{EnergyModel m;verified(m);m.fail("offline",4000);auto h=m.history(0);CHECK(!h.empty());CHECK(std::isnan(h.back()));}},
        {"unsigned clock rollover preserves freshness",[]{EnergyModel m;m.ingest(sample(),0xfffffff0u);m.ingest(sample(100,"2026-11-13 10:00:01"),0xfffffff8u);m.tick(30);CHECK(m.complete);}},
        {"Gen1 v2 response parses meters and cumulative Wh",[]{
            const char *s=R"([{"id":"485519d9cf2f","online":1,"status":{"_updated":"a","emeters":[{"power":12,"is_valid":true,"voltage":231,"total":1000},{"power":34,"is_valid":true,"voltage":231,"total":2000}]}},{"id":"485519d9502e","online":1,"status":{"_updated":"b","emeters":[{"power":56,"is_valid":true},{"power":78,"is_valid":true}]}}])";
            RawReadings r;CHECK(parseCloudReadings(s,EnergyConfig{},r));CHECK(r[0].power==34);CHECK(r[2].power==12);CHECK(r[1].power==78);CHECK(r[3].power==56);CHECK(r[0].energyWh==2000);CHECK(r[0].marker=="a");
        }},
        {"malformed responses and booleans cannot masquerade as watts",[]{
            RawReadings r;CHECK(!parseCloudReadings("{",EnergyConfig{},r));CHECK(!parseCloudReadings("{}",EnergyConfig{},r));
            CHECK(parseCloudReadings(R"([{"id":"485519d9cf2f","online":true,"status":{"emeters":[{"power":true,"is_valid":true}]}}])",EnergyConfig{},r));CHECK(std::isnan(r[2].power));CHECK(!r[1].online);
        }},
        {"1PM and EM1 energy remain subordinate",[]{
            EnergyConfig c;c.sources[4]={"Boiler","aabbccddeeff","switch:0",0};c.sources[5]={"Boiler 2","aabbccddeeff","em1:0",1};
            RawReadings r;CHECK(parseCloudReadings(R"([{"id":"aabbccddeeff","online":1,"status":{"_updated":"c","switch:0":{"apower":900,"aenergy":{"total":3210}},"em1:0":{"act_power":750,"voltage":230},"em1data:0":{"total_act_energy":6543}}}])",c,r));
            CHECK(r[4].power==900);CHECK(r[4].energyWh==3210);CHECK(r[5].power==750);CHECK(r[5].energyWh==6543);
        }}
    };
    for(auto &c:cases){c.second();std::cout<<"PASS "<<c.first<<'\n';}
    std::cout<<cases.size()<<" energy tests passed\n";
}
