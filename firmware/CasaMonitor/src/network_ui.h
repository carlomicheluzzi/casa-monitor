#pragma once
#include "wifi_platform.h"
#include <lvgl.h>
#include "energy_ui.h"

namespace casa {
enum class UiAction { None, Scan, Apply, Retry };
struct UiRequest { UiAction action = UiAction::None; WifiConfig config; bool keepPassword = false; };

// Used unchanged by ESP32 and by the desktop headless render/interaction tests.
class NetworkUi {
public:
    EnergyUi energy;
    void init();
    void update(WifiModel &model);
    UiRequest takeRequest();
    void scanResults(const std::vector<ScanNetwork> &networks, const std::string &error);
    void error(const std::string &message);
    void showSettings();
    void showNetwork();
    void showHome();
    // Stable widget handles are also used by the host interaction tests.
    lv_obj_t *settingsButton=nullptr, *networkButton=nullptr, *saveButton=nullptr;
    lv_obj_t *ssid=nullptr, *password=nullptr, *openCheck=nullptr, *addressMode=nullptr;
    lv_obj_t *ip=nullptr, *mask=nullptr, *gateway=nullptr, *dns=nullptr, *keyboard=nullptr;
    lv_obj_t *scanButton=nullptr, *retryButton=nullptr;
    lv_obj_t *settingsPanel()const{return settings;}
    bool homeVisible()const{return page==0;}
    bool networkVisible() const { return page == 2; }
private:
    lv_obj_t *home=nullptr, *settings=nullptr, *network=nullptr, *form=nullptr, *listPanel=nullptr;
    lv_obj_t *list=nullptr, *status=nullptr, *current=nullptr, *homeStatus=nullptr, *actions=nullptr;
    lv_obj_t *staticFields=nullptr, *showPassword=nullptr;
    std::vector<ScanNetwork> found;
    WifiConfig cached;
    UiRequest request;
    bool configured=false, busy=false, scanning=false;
    bool transientError=false;
    unsigned seenOpen=0;
    int page=0;
    std::string lastMessage;
    static void event(lv_event_t *e);
    void handle(lv_event_t *e);
    void keyboardVisible(bool visible);
    void fill();
    void queueScan();
    void submit();
    void modeChanged();
};
}
