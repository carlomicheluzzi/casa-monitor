#include "device_platform.h"
#include "board/rgb_lcd_port/rgb_lcd_port.h"
#include <Preferences.h>
#include <Arduino.h>
#include <mutex>
namespace {
std::mutex displayMutex;
casa::ScreenPolicy policy;
unsigned brightness=70,appliedBrightness=0;
bool lit=true,maintenanceMode=false,blockedTouch=false,lastPressed=false;
bool lastInhibit=false,lastMissing=false,testTimeout=false;
uint32_t configuredTimeout=120,touchPresses=0,testStarted=0;
}
namespace casa {
bool loadDeviceOptions(DeviceOptions &out){Preferences p;if(!p.begin("casa-device",true))return false;auto text=p.getString("config","");p.end();return parseOptions(text.c_str(),out);}
bool saveDeviceOptions(const DeviceOptions &v){if(!validateOptions(v).empty())return false;auto text=encodeOptions(v);Preferences p;if(!p.begin("casa-device",false))return false;bool ok=p.putString("config",text.c_str())==text.size();p.end();return ok;}
void configureDisplay(const DeviceOptions &v){std::lock_guard<std::mutex> guard(displayMutex);policy.configure(v,millis());brightness=v.brightness;configuredTimeout=v.screenTimeout;testTimeout=false;}
DisplayStatus displayStatus(){std::lock_guard<std::mutex> guard(displayMutex);return {policy.asleep,lit,lastInhibit,lastMissing,testTimeout,policy.idle(millis()),policy.timeout(),configuredTimeout,touchPresses};}
bool testDisplayTimeout(uint32_t seconds){
    if(seconds&&(seconds<15||seconds>120))return false;
    std::lock_guard<std::mutex> guard(displayMutex);testTimeout=seconds!=0;testStarted=millis();policy.setTimeout(seconds?seconds:configuredTimeout,testStarted);return true;
}
void tickDisplay(bool alarm,bool missing,bool inhibit,bool maintenance){
    bool desired;unsigned level;
    {std::lock_guard<std::mutex> guard(displayMutex);maintenanceMode=maintenance;lastInhibit=inhibit;lastMissing=missing;
        if(testTimeout&&uint32_t(millis()-testStarted)>=300000){testTimeout=false;policy.setTimeout(configuredTimeout,millis());}
        policy.tick(millis(),alarm,missing,inhibit);desired=(!policy.asleep&&!maintenance)||alarm;level=brightness;}
    if(level!=appliedBrightness){IO_EXTENSION_Pwm_Output(100-level);appliedBrightness=level;}
    if(desired!=lit){if(desired)waveshare_rgb_lcd_bl_on();else waveshare_rgb_lcd_bl_off();lit=desired;}
}
}
extern "C" bool casa_display_touch(bool pressed){std::lock_guard<std::mutex> guard(displayMutex);if(pressed&&!lastPressed)++touchPresses;lastPressed=pressed;if(maintenanceMode||blockedTouch){blockedTouch=pressed;policy.wake(millis());return false;}return policy.touch(pressed,millis());}
