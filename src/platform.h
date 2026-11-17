#pragma once
// platform.h - OS abstraction (declarations only).
#include <algorithm>
#include <atomic>
#include <chrono>
#include <string>
#include "types.h"

namespace ddr5 {

namespace plat {
void install_ctrlc(std::atomic<bool>* s); // Ctrl-C / SIGINT sets *s
// Logical CPUs this process may run on, not the machine's CPU count
int ncpu();
// Pin the calling thread to the cpu-th usable CPU: the cpu-th of the allowed set (Windows
bool pin(int cpu);
// Free and installed physical memory in bytes.
u64 avail_phys();
u64 total_phys();
// What this process can still commit, in bytes (Windows: RAM + page file
u64 avail_commit();
void lower_priority();
// Keep the machine from going to sleep because nobody touched it (Windows
enum class KeepAwake { NotSupported, On, Failed };
KeepAwake keep_awake(bool on);
// Locking is the first touch of every page, so for tens of GB it takes many seconds.
inline constexpr u64 kLockSlice = 64ull << 20;
struct LockProgress {
    u64 done = 0;
    bool failed = false;
    bool stopped = false;
};
// Calls lock(p + off, n) -> bool for consecutive slices of at most `slice` bytes
template <class LockFn>
LockProgress lock_in_slices(u8* p, u64 bytes, const std::atomic<bool>* stop, u64 slice, LockFn lock) {
    LockProgress r;
    if (!slice)
        slice = bytes;
    while (r.done < bytes) {
        if (stop && stop->load(std::memory_order_relaxed)) {
            r.stopped = true;
            break;
        }
        const u64 n = std::min(slice, bytes - r.done);
        if (!lock(p + r.done, n)) {
            r.failed = true;
            break;
        }
        r.done += n;
    }
    return r;
}
// got = large pages obtained; locked = the memory is really pinned in RAM (large pages always
void* alloc(u64 bytes, bool large, bool& got, bool& locked, std::string& note, const std::atomic<bool>* stop = nullptr);
void release(void* p, u64 bytes);
inline double now() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}
} // namespace plat

} // namespace ddr5
