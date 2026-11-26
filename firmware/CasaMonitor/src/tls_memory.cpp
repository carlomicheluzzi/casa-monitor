#include "tls_memory.h"
#include <mbedtls/platform.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <atomic>
#include <cstdint>
#include <cstdlib>

namespace {
std::atomic<TaskHandle_t> networkTask{nullptr};
std::atomic<uint32_t> externalAllocations{0},externalFailures{0};
bool installed=false;
void *tlsCalloc(size_t count,size_t size) {
    if(size&&count>SIZE_MAX/size)return nullptr;
    // ESP-IDF's default uses INTERNAL|8BIT for every mbedTLS allocation.
    // Keep small crypto objects and other tasks on that same allocation path.
    // Only large buffers owned by the lower-priority cloud/OTA task use PSRAM.
    if(networkTask.load()==xTaskGetCurrentTaskHandle()&&count*size>=4096) {
        void *p=heap_caps_calloc(count,size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if(p)externalAllocations.fetch_add(1);else externalFailures.fetch_add(1);
        return p; // Preserve internal SRAM headroom even when external memory is exhausted.
    }
    return heap_caps_calloc(count,size,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
}
void tlsFree(void *p){heap_caps_free(p);}
}
namespace casa {
void initTlsMemory(){
    // mbedTLS supports these hooks in the pinned core's MBEDTLS_PLATFORM_MEMORY
    // configuration. Install before any Wi-Fi or TLS work; never change them live.
    int result=mbedtls_platform_set_calloc_free(tlsCalloc,tlsFree);
    if(result!=0)std::abort();
    installed=true;
}
void registerTlsMemoryTask(){networkTask.store(xTaskGetCurrentTaskHandle());}
TlsMemoryStats tlsMemoryStats(){return {installed,externalAllocations.load(),externalFailures.load()};}
}
