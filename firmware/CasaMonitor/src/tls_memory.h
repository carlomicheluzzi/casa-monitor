#pragma once
#include <cstdint>
namespace casa {
// Install once before Wi-Fi starts. Only the existing cloud/OTA task opts in.
void initTlsMemory();
void registerTlsMemoryTask();
struct TlsMemoryStats {bool installed=false;uint32_t externalAllocations=0,externalFailures=0;};
TlsMemoryStats tlsMemoryStats();
}
