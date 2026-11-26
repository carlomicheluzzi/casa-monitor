#pragma once
#include "device_model.h"
#include <lvgl.h>
namespace casa {
enum class DeviceAction {None,Save,Discover,Check,Install,OpenDisplay,OpenLocal,OpenUpdates};
struct DeviceRequest {DeviceAction action=DeviceAction::None;DeviceOptions options;};
class DeviceUi {
public:
    bool deferOpen=false; // firmware releases cloud TLS memory before allocating a configuration page
    void init(lv_obj_t *settings);
    void update(const DeviceOptions &options,const std::vector<LocalDeviceView> &devices,const UpdateView &update);
    DeviceRequest takeRequest(){auto r=request;request={};return r;}
    void result(const std::string &message);
    bool visible()const{return overlay!=nullptr;}
    void close();
    void open(int page);
    lv_obj_t *displayButton=nullptr,*lanButton=nullptr,*updateButton=nullptr;
    lv_obj_t *saveButton=nullptr,*checkButton=nullptr,*installButton=nullptr,*discoverButton=nullptr;
    lv_obj_t *timeout=nullptr,*brightness=nullptr,*localCheck=nullptr,*fallbackCheck=nullptr,*wakeCheck=nullptr;
    lv_obj_t *deviceList=nullptr,*deviceId=nullptr,*host=nullptr,*username=nullptr,*password=nullptr,*clearPassword=nullptr;
    lv_obj_t *url=nullptr,*dailyCheck=nullptr,*autoCheck=nullptr,*keyboard=nullptr;
private:
    DeviceOptions cached;std::vector<LocalDeviceView> devices;std::vector<LanDevice> endpoints;UpdateView updater;
    std::string listText;bool transient=false;
    DeviceRequest request;int page=0;lv_obj_t *overlay=nullptr,*form=nullptr,*actions=nullptr,*status=nullptr,*progress=nullptr;
    static void event(lv_event_t *e);void handle(lv_event_t *e);void submit();void fillEndpoint();void refreshList(bool initial=false);void keyboardVisible(bool on);
};
}
