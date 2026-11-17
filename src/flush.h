#pragma once
// flush.h - cache-line flush used by every "flush = 1" access
#include "types.h"

namespace ddr5 {

enum class FlushMode : int {
    Auto, // CLFLUSHOPT when the CPU has it, otherwise CLFLUSH
    ClFlush,
    ClFlushOpt
};

bool cpu_has_clflushopt(); // CPUID.(EAX=7,ECX=0):EBX bit 23
void set_flush_mode(FlushMode m);
bool flush_uses_opt(); // what the hot path will execute now
const char* flush_mode_name(); // "CLFLUSHOPT" or "CLFLUSH", for the log

extern bool g_flush_opt;

inline void flush_line(const void* p) {
    if (g_flush_opt) {
#if defined(_MSC_VER)
        _mm_clflushopt((void*)p);
#else
        // inline asm so no -mclflushopt is needed (the build only enables AVX2)
        asm volatile("clflushopt %0" ::"m"(*(const char*)p) : "memory");
#endif
    } else {
        _mm_clflush(p);
    }
}

} // namespace ddr5
