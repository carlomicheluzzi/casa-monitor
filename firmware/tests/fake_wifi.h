#pragma once
#include "wifi_model.h"

struct FakeWifi : casa::WifiPlatform {
    casa::WifiConfig stored, target;
    bool present=false, connected=false, writable=true, connectable=true;
    int saves=0, connects=0;
    bool load(casa::WifiConfig &c) override { c=stored; return present; }
    bool save(const casa::WifiConfig &c) override {
        ++saves;
        if(!writable) return false;
        stored=c; present=true; return true;
    }
    bool connect(const casa::WifiConfig &c) override { target=c; ++connects; return connectable; }
    void disconnect() override { connected=false; }
    bool hasIP() override { return connected; }
    std::string address() override { return target.dhcp?"192.168.1.80":target.ip; }
};

inline casa::WifiConfig homeConfig() { return {"Casa", "password-di-prova", false, true, "", "", "", ""}; }
