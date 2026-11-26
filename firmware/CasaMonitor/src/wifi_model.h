#pragma once
#include <cstdint>
#include <string>

namespace casa {
struct WifiConfig {
    std::string ssid, password;
    bool open = false;
    bool dhcp = true;
    std::string ip, mask, gateway, dns;
};

bool ipv4(const std::string &text, uint32_t &value);
std::string validate(const WifiConfig &config);

class WifiPlatform {
public:
    virtual ~WifiPlatform() = default;
    virtual bool load(WifiConfig &config) = 0;
    virtual bool save(const WifiConfig &config) = 0;
    virtual bool connect(const WifiConfig &config) = 0;
    virtual void disconnect() = 0;
    virtual bool hasIP() = 0;
    virtual std::string address() = 0;
};

enum class Connection { Unconfigured, Connecting, Online, Offline };

// All methods run on the application task, never on a Wi-Fi event callback.
class WifiModel {
public:
    static constexpr uint32_t TIMEOUT_MS = 25000;
    static constexpr uint32_t RETRY_MS = 30000;
    explicit WifiModel(WifiPlatform &platform) : io(platform) {}
    void start(uint32_t now);
    void tick(uint32_t now);
    std::string apply(WifiConfig config, bool keepPassword, uint32_t now);
    void retry(uint32_t now);
    Connection state = Connection::Unconfigured;
    WifiConfig saved;
    WifiConfig attempted;
    bool configured = false;
    bool trial = false;
    unsigned openRequest = 0;
    std::string message;
    std::string ip() { return state == Connection::Online ? io.address() : "—"; }
private:
    WifiPlatform &io;
    uint32_t since = 0;
    bool reported = false, restoring = false;
    void begin(const WifiConfig &config, uint32_t now);
    void failed(uint32_t now, const std::string &reason);
};
}
