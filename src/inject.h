// inject.h - fault-injection hooks for tools/selftest.sh.
#pragma once

#ifdef DDR5TEST_INJECT
#include "audit.h"
#include "order.h"
#include "run.h"
#include "types.h"
#include <cstddef>

namespace ddr5::inject {

using StoreFn = void (*)(void*, const Line&); // IO<...>::store of the calling test

void after_fill(Run& R, int tid, size_t ji, const View& w);
void march_after_element(int tid, size_t ji, size_t ei, const View& w);
void chase_after_fill(int tid, u64 sd, u64 K, const View& w, const Order& o, u64 steps);
void copy_after_copy(int tid, size_t ji, u8* A, u8* B, u64 nA);
void copy_fill_stop(Run& R, int tid, u64 i);
void rwstorm_before_final(int tid, size_t ji, const View& w);
void rwstorm_read_fault(int tid, size_t ji, u64 i, Line& e);
void evolve_last_pass(Run& R, int tid, size_t ji, const View& w, u64 p, u64 passes, u64 seed, u64 maxp, StoreFn store);
void evolve_stop_in_write(Run& R, int tid, u64 p);
void after_fill_throw(int tid);
void extra_sync(Run& R, int tid);
void pagemix_fill_stop(Run& R, int tid, u64 pg);
void pagemix_flip(Run& R, int tid);
void pagemix_final_stop(Run& R, int tid, u64 pg);
void coherency_extra(u8* mem, int tid);

// E2f: the audit of the coverage map (audit_line is declared in audit.h)
void audit_begin(Run& R);  // after the region is allocated and the map sized: starts a clean audit
void audit_report(Run& R); // end of the run: prints one "audit:" line comparing the map with the audit
// Fill verify of window `vw`: 0 = verify and mark as usual; with DDR5TEST_SKIP_WINDOW set, thread 0's first
// window is skipped: 1 = no verify and no mark (a window the algo really skipped), 2 = no verify but still
// marked (a lie, so the audit must catch it).
int skip_window(Run& R, int tid, const View& vw);

} // namespace ddr5::inject

#define DDR5_INJECT(hook, ...) ::ddr5::inject::hook(__VA_ARGS__)
#define DDR5_INJECT_IF(hook, ...) ::ddr5::inject::hook(__VA_ARGS__) // a hook that returns a value
#else
#define DDR5_INJECT(hook, ...) ((void)0)
#define DDR5_INJECT_IF(hook, ...) (0)
#endif
