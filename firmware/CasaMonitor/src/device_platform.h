#pragma once
#include "device_model.h"
namespace casa {
bool loadDeviceOptions(DeviceOptions &options);
bool saveDeviceOptions(const DeviceOptions &options);
void configureDisplay(const DeviceOptions &options);
void tickDisplay(bool alarm,bool missing,bool inhibit,bool maintenance);
struct DisplayStatus {bool asleep=false,backlightOn=true,inhibited=false,missing=false,testTimeout=false;uint32_t idleMs=0,timeoutSeconds=0,savedTimeoutSeconds=0,touchPresses=0;};
DisplayStatus displayStatus();
// Non-persistent USB diagnostic. 0 restores the saved timeout; automatically
// restores it after five minutes so a short test never changes daily use.
bool testDisplayTimeout(uint32_t seconds);
}
extern "C" bool casa_display_touch(bool pressed);
