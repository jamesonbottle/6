#pragma once
// run_data.h - the data model of a run: Chunk, View, Counter, Stats, Layout, Progress, ErrorSink.
// Plain data and its counters only; the behaviour (Run, parallel, walk) is in run.h.
#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>
#include "types.h"

namespace ddr5 {

// run context Windows (window_mb) are rounded down to a multiple of this many LINES (256 KiB)
constexpr u64 kWindowGranuleLines = 4096;
struct Chunk {
    u8* base;
    u64 lines;
    u64 g;
};
struct View {
    u8* base;
    u64 g;
    u64 n;
};
// A shared counter alone on its cache line
struct alignas(kLineBytes) Counter : std::atomic<u64> {
    Counter() : std::atomic<u64>(0) {}
    using std::atomic<u64>::operator=;
};
static_assert(sizeof(Counter) == kLineBytes, "one counter per cache line");

struct Stats {
    std::atomic<u64> lines{0}, bits{0}, blk[4], trans{0}, stable{0}, unstable{0};
    Counter lane[32], beat[16], abit[48]; // the hot ones: every error bumps several of them
    Stats() { clear(); }
    void clear() {
        lines = 0;
        bits = 0;
        trans = 0;
        stable = 0;
        unstable = 0;
        for (auto& a : lane)
            a = 0;
        for (auto& a : beat)
            a = 0;
        for (auto& a : blk)
            a = 0;
        for (auto& a : abit)
            a = 0;
    }
};
// lines this thread has verified since it last published them to Progress (see Run::vtick)
inline thread_local u64 t_vcount = 0;

// Where the tested memory is and how it is split between the worker threads.
struct Layout {
    u8* region = nullptr;
    std::atomic<u64> regionBytes{0};
    int T = 1;                 // worker threads
    std::vector<Chunk> chunks; // one per thread
};

// How far the run has got: the byte counters the workers bump, the clocks
struct Progress {
    std::atomic<u64> done{0};
    // passes (HCI/Karhu-style: how many times the tested region has been verified, in bytes)
    std::atomic<u64> vbytes{0}, vtotal{0};
    std::atomic<double> tStart{0};
    double tTestStart = 0; // plat::now() at the start of the current test
    u64 stopCoverage = 0;
    double stopSeconds = 0; // stop after this many seconds (0 = off)
    std::string stopReason;
};

// Error reporting: the log file, the "first error" times, the counters.
struct ErrorSink {
    mutable std::mutex mu; // mutable: Run::status() is const but has to lock it
    FILE* logf = nullptr;
    std::atomic<u64> logged{0};
    // seconds into the run / the current test; -1 = no error yet.
    std::atomic<double> firstErrRun{-1}, firstErrTest{-1};
    std::atomic<u64> errTotal{0};
    // set (once, with a console warning) when a write to errors.logf failed (disk full, device gone)
    std::atomic<bool> logFailed{false};
};

} // namespace ddr5
