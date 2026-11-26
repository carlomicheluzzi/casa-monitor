#include "wifi_platform.h"
#include <WiFi.h>
#include <Preferences.h>
#include <esp_wifi.h>
#include <cstring>
#include <algorithm>

namespace casa {
namespace {
// One NVS value: no partially saved set of SSID/password/IP keys.
struct StoredConfig {
    uint32_t magic;
    char ssid[33], password[65];
    uint8_t open, dhcp;
    char ip[16], mask[16], gateway[16], dns[16];
};
constexpr uint32_t MAGIC = 0x43574631;
template<size_t N> bool terminated(const char (&s)[N]) { return memchr(s, '\0', N); }
template<size_t N> void copy(char (&dst)[N], const std::string &value) {
    memcpy(dst, value.data(), std::min(value.size(), N-1));
}
IPAddress ip(const std::string &text) {
    IPAddress result;
    result.fromString(text.c_str());
    return result;
}
}

void EspWifiPlatform::init() {
    WiFi.onEvent([this](arduino_event_id_t event, arduino_event_info_t) {
        if(event==ARDUINO_EVENT_WIFI_STA_GOT_IP) ready.store(true);
        if(event==ARDUINO_EVENT_WIFI_STA_DISCONNECTED || event==ARDUINO_EVENT_WIFI_STA_LOST_IP ||
           event==ARDUINO_EVENT_WIFI_STA_STOP) ready.store(false);
    });
    WiFi.persistent(false); // The application commits only a successfully tested config.
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(false); // One retry policy, owned by WifiModel.
    WiFi.setHostname("casa-energia");
    WiFi.setSleep(false);
    WiFi.setMinSecurity(WIFI_AUTH_OPEN); // Open networks require explicit UI selection.
}

bool EspWifiPlatform::load(WifiConfig &c) {
    Preferences store;
    if (!store.begin("casa-wifi", true)) return false;
    StoredConfig s{};
    bool ok = store.getBytesLength("config") == sizeof(s) &&
              store.getBytes("config", &s, sizeof(s)) == sizeof(s);
    store.end();
    if (!ok || s.magic != MAGIC || s.open > 1 || s.dhcp > 1 || !terminated(s.ssid) ||
        !terminated(s.password) || !terminated(s.ip) || !terminated(s.mask) ||
        !terminated(s.gateway) || !terminated(s.dns)) return false;
    c = {s.ssid, s.password, bool(s.open), bool(s.dhcp), s.ip, s.mask, s.gateway, s.dns};
    return validate(c).empty();
}

bool EspWifiPlatform::save(const WifiConfig &c) {
    if (!validate(c).empty()) return false;
    StoredConfig s{};
    s.magic = MAGIC;
    copy(s.ssid,c.ssid); copy(s.password,c.password);
    s.open = c.open; s.dhcp = c.dhcp;
    copy(s.ip,c.ip); copy(s.mask,c.mask); copy(s.gateway,c.gateway); copy(s.dns,c.dns);
    Preferences store;
    if (!store.begin("casa-wifi", false)) return false;
    bool ok = store.putBytes("config", &s, sizeof(s)) == sizeof(s);
    store.end();
    return ok;
}

void EspWifiPlatform::disconnect() {
    if (scanning) { esp_wifi_scan_stop(); WiFi.scanDelete(); scanning = false; }
    ready.store(false);
    WiFi.disconnect(false, false); // Do not erase the saved application config.
}

bool EspWifiPlatform::connect(const WifiConfig &c) {
    expectedSsid=c.ssid;
    // Use the native STA API: the legacy WiFi.config wrapper reorders arguments
    // for masks whose first octet is not 255 (valid /1–/7 networks).
    bool ok = WiFi.STA.begin() && (c.dhcp ? WiFi.STA.config(INADDR_NONE, INADDR_NONE, INADDR_NONE) :
        WiFi.STA.config(ip(c.ip), ip(c.gateway), ip(c.mask), ip(c.dns)));
    if (!ok) return false;
    WiFi.begin(c.ssid.c_str(), c.open ? nullptr : c.password.c_str());
    return true;
}

bool EspWifiPlatform::hasIP() {
    return ready.load() && WiFi.status() == WL_CONNECTED &&
           WiFi.SSID()==expectedSsid.c_str() && uint32_t(WiFi.localIP()) != 0;
}
std::string EspWifiPlatform::address() { return WiFi.localIP().toString().c_str(); }

void EspWifiPlatform::scan() {
    if (scanning) return;
    WiFi.scanDelete();
    scanning = true;
    WiFi.scanNetworks(true, true);
}

bool EspWifiPlatform::scanFinished(std::vector<ScanNetwork> &networks, std::string &error) {
    if (!scanning) return false;
    int count = WiFi.scanComplete();
    if (count == WIFI_SCAN_RUNNING) return false;
    networks.clear();
    if (count < 0) error = "Ricerca non riuscita. Riprova o inserisci il nome rete.";
    for (int i=0; i<count; ++i) {
        std::string ssid = WiFi.SSID(i).c_str();
        if (ssid.empty()) continue; // Hidden SSIDs are entered manually.
        auto auth = WiFi.encryptionType(i);
        bool open = auth == WIFI_AUTH_OPEN;
        bool supported = open || auth == WIFI_AUTH_WPA_PSK || auth == WIFI_AUTH_WPA2_PSK ||
            auth == WIFI_AUTH_WPA_WPA2_PSK || auth == WIFI_AUTH_WPA3_PSK ||
            auth == WIFI_AUTH_WPA2_WPA3_PSK;
        if (std::none_of(networks.begin(), networks.end(), [&](const ScanNetwork &n) { return n.ssid == ssid; }))
            networks.push_back({ssid, WiFi.RSSI(i), open, supported});
    }
    std::sort(networks.begin(), networks.end(), [](const ScanNetwork &a, const ScanNetwork &b) { return a.rssi > b.rssi; });
    if (networks.size() > 20) networks.resize(20);
    WiFi.scanDelete();
    scanning = false;
    return true;
}
}
