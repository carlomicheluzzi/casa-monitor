#include "usb_update_model.h"
#include "device_model.h"
#include <cJSON.h>
namespace casa {
UpdateUsbRequest parseUpdateUsb(const cJSON *root){
    const auto *configure=cJSON_GetObjectItemCaseSensitive(root,"casa_updates_configure");
    const auto *check=cJSON_GetObjectItemCaseSensitive(root,"casa_updates_check");
    if(!configure&&!check)return {};
    if((configure&&check)||!cJSON_IsObject(root))return {UpdateUsbAction::Invalid,{}};
    const auto *flag=configure?configure:check;
    if(!cJSON_IsNumber(flag)||flag->valuedouble!=1)return {UpdateUsbAction::Invalid,{}};
    if(check)return {UpdateUsbAction::Check,{}};
    const auto *url=cJSON_GetObjectItemCaseSensitive(root,"manifest_url");
    if(!cJSON_IsString(url)||!url->valuestring||!httpsUrlAllowed(url->valuestring))return {UpdateUsbAction::Invalid,{}};
    return {UpdateUsbAction::Configure,url->valuestring};
}
}
