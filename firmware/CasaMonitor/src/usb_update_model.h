#pragma once
#include <string>
struct cJSON;
namespace casa {
enum class UpdateUsbAction {None,Configure,Check,Invalid};
struct UpdateUsbRequest {UpdateUsbAction action=UpdateUsbAction::None;std::string manifestUrl;};
UpdateUsbRequest parseUpdateUsb(const cJSON *root);
}
