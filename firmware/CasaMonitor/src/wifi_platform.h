#pragma once
#include "wifi_model.h"
#include <vector>
#include <atomic>

namespace casa {
struct ScanNetwork {
    std::string ssid;
    int rssi;
    bool open;
    bool supported;
};

class EspWifiPlatform : public WifiPlatform {
public:
    void init();
    bool load(WifiConfig &config) override;
    bool save(const WifiConfig &config) override;
    bool connect(const WifiConfig &config) override;
    void disconnect() override;
    bool hasIP() override;
    std::string address() override;
    void scan();
    bool scanFinished(std::vector<ScanNetwork> &networks, std::string &error);
private:
    bool scanning = false;
    std::atomic<bool> ready{false};
    std::string expectedSsid;
};
}
