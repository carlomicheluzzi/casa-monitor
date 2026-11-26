#include "fake_wifi.h"
#include <iostream>
#include <stdexcept>
#include <functional>
#include <vector>

#define CHECK(x) do { if(!(x)) throw std::runtime_error("Check failed, line " + std::to_string(__LINE__) + ": " #x); } while(0)
using namespace casa;

int main() {
    std::vector<std::pair<const char *,std::function<void()>>> cases={
        {"first boot opens setup without writing flash",[]{
            FakeWifi io; WifiModel m(io); m.start(0);
            CHECK(m.state==Connection::Unconfigured); CHECK(m.openRequest==1); CHECK(io.saves==0);
        }},
        {"saved network connects without opening setup",[]{
            FakeWifi io; io.present=true; io.stored=homeConfig(); WifiModel m(io); m.start(0);
            CHECK(m.state==Connection::Connecting); CHECK(m.openRequest==0);
            io.connected=true; m.tick(10);
            CHECK(m.state==Connection::Online); CHECK(io.saves==0); CHECK(m.ip()=="192.168.1.80");
        }},
        {"25 second timeout opens setup; subsequent retries do not reopen it",[]{
            FakeWifi io; io.present=true; io.stored=homeConfig(); WifiModel m(io); m.start(100);
            m.tick(25099); CHECK(m.openRequest==0);
            m.tick(25100); CHECK(m.openRequest==1); CHECK(m.state==Connection::Offline);
            m.tick(55100); CHECK(m.state==Connection::Connecting); CHECK(m.openRequest==1);
            m.tick(80100); CHECK(m.openRequest==1); CHECK(io.saves==0);
        }},
        {"candidate is saved only after getting an IP and survives reboot",[]{
            FakeWifi io; WifiModel m(io); m.start(0);
            CHECK(m.apply(homeConfig(),false,10).empty()); CHECK(io.saves==0);
            m.tick(20); CHECK(!m.configured);
            io.connected=true; m.tick(21); CHECK(m.configured); CHECK(io.saves==1);
            WifiModel reboot(io); reboot.start(0); CHECK(reboot.attempted.ssid=="Casa");
        }},
        {"failed candidate rolls back without overwriting old credentials",[]{
            FakeWifi io; io.present=true; io.stored=homeConfig(); WifiModel m(io); m.start(0);
            io.connected=true; m.tick(1);
            auto next=homeConfig(); next.ssid="Ufficio";
            CHECK(m.apply(next,false,10).empty()); m.tick(25010);
            CHECK(io.target.ssid=="Casa"); CHECK(io.stored.ssid=="Casa"); CHECK(io.saves==0);
            io.connected=true; m.tick(25011);
            CHECK(m.state==Connection::Online); CHECK(m.message.find("Ripristinata")!=std::string::npos);
        }},
        {"failed first trial does not persist a bad network",[]{
            FakeWifi io; WifiModel m(io); m.start(0); m.apply(homeConfig(),false,1); m.tick(25001);
            CHECK(!m.configured); CHECK(!io.present); CHECK(!m.trial); CHECK(m.state==Connection::Offline);
        }},
        {"flash failure keeps the last working network",[]{
            FakeWifi io; io.present=true; io.stored=homeConfig(); WifiModel m(io); m.start(0);
            io.connected=true; m.tick(1); io.writable=false;
            auto next=homeConfig(); next.ssid="Ufficio"; m.apply(next,false,2);
            io.connected=true; m.tick(3);
            CHECK(m.state==Connection::Connecting); CHECK(io.target.ssid=="Casa"); CHECK(m.saved.ssid=="Casa");
        }},
        {"link loss retries before opening setup",[]{
            FakeWifi io; io.present=true; io.stored=homeConfig(); WifiModel m(io); m.start(0);
            io.connected=true; m.tick(1); io.connected=false; m.tick(2);
            CHECK(m.state==Connection::Connecting); CHECK(m.openRequest==0);
            m.tick(25002); CHECK(m.openRequest==1);
        }},
        {"short outages recover without interrupting the dashboard",[]{
            FakeWifi io; io.present=true; io.stored=homeConfig(); WifiModel m(io); m.start(0);
            io.connected=true; m.tick(1); io.connected=false; m.tick(2); io.connected=true; m.tick(100);
            CHECK(m.state==Connection::Online); CHECK(m.openRequest==0);
        }},
        {"millis rollover does not prevent fallback",[]{
            FakeWifi io; io.present=true; io.stored=homeConfig(); WifiModel m(io); m.start(0xfffffff0u);
            m.tick(0xfffffff0u+25000u); CHECK(m.state==Connection::Offline); CHECK(m.openRequest==1);
        }},
        {"blank password is retained only for the same protected SSID",[]{
            FakeWifi io; io.present=true; io.stored=homeConfig(); WifiModel m(io); m.start(0);
            io.connected=true; m.tick(1);
            auto c=homeConfig(); c.ssid="Altra"; c.password="";
            CHECK(!m.apply(c,true,2).empty()); CHECK(io.connects==1);
            c.ssid="Casa"; CHECK(m.apply(c,true,3).empty()); CHECK(io.target.password==io.stored.password);
        }},
        {"cannot replace an in-flight trial",[]{
            FakeWifi io; WifiModel m(io); m.start(0); m.apply(homeConfig(),false,1);
            CHECK(!m.apply(homeConfig(),false,2).empty()); CHECK(io.connects==1);
        }},
        {"manual settings can replace a background retry immediately",[]{
            FakeWifi io; io.present=true; io.stored=homeConfig(); WifiModel m(io); m.start(0);
            auto c=homeConfig(); c.ssid="Nuova rete";
            CHECK(m.apply(c,false,1).empty()); CHECK(io.target.ssid=="Nuova rete"); CHECK(m.trial);
        }},
        {"open networks require an explicit selection and discard password",[]{
            FakeWifi io; WifiModel m(io); m.start(0); auto c=homeConfig(); c.open=true;
            CHECK(m.apply(c,false,1).empty()); CHECK(io.target.password.empty());
        }},
        {"bad NVS data falls back to setup",[]{
            FakeWifi io; io.present=true; WifiModel m(io); m.start(0);
            CHECK(!m.configured); CHECK(m.openRequest==1); CHECK(io.connects==0);
        }},
        {"driver failure is recoverable",[]{
            FakeWifi io; io.connectable=false; WifiModel m(io); m.start(0); m.apply(homeConfig(),false,1);
            CHECK(m.state==Connection::Offline); CHECK(!io.present);
        }},
        {"SSID byte limit, spaces, passphrase and raw PSK validation",[]{
            auto c=homeConfig(); c.ssid="  Casa  "; CHECK(validate(c).empty());
            c.ssid=std::string(33,'a'); CHECK(!validate(c).empty()); c.ssid="Casa";
            c.password="short"; CHECK(!validate(c).empty());
            c.password=std::string(64,'a'); CHECK(validate(c).empty());
            c.password=std::string(64,'z'); CHECK(!validate(c).empty());
            c.password=std::string("abc\0defgh",9); CHECK(!validate(c).empty());
        }},
        {"static IPv4 requires a valid subnet, gateway and DNS",[]{
            auto c=homeConfig(); c.dhcp=false; c.ip="192.168.1.80"; c.mask="255.255.255.0";
            c.gateway="192.168.1.1"; c.dns="1.1.1.1"; CHECK(validate(c).empty());
            c.ip="192.168.1.0"; CHECK(!validate(c).empty());
            c.ip="192.168.1.255"; CHECK(!validate(c).empty()); c.ip="192.168.1.80";
            c.gateway="192.168.2.1"; CHECK(!validate(c).empty()); c.gateway="192.168.1.1";
            c.mask="255.0.255.0"; CHECK(!validate(c).empty()); c.mask="255.255.255.0";
            c.dns="0.0.0.0"; CHECK(!validate(c).empty());
        }},
        {"strict IPv4 parsing rejects malformed and trailing input",[]{
            uint32_t v;
            for(auto s:{"1.2.3","1.2.3.256","1.2.3.4.5"," 1.2.3.4","1.2.3.4x","1..3.4","-1.2.3.4"}) CHECK(!ipv4(s,v));
            CHECK(ipv4("192.168.1.80",v)); CHECK(v==0xc0a80150u);
        }}
    };
    for(auto &c:cases) { c.second(); std::cout<<"PASS "<<c.first<<'\n'; }
    std::cout<<cases.size()<<" network policy tests passed\n";
}
