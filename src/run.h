#pragma once
// run.h - Chunk, View, Stats, Layout, Progress, ErrorSink, Run, walk().
#include <algorithm>
#include <atomic>
#include <exception>
#include <barrier>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>
#include "coverage.h"
#include "log.h"
#include "order.h"
#include "params.h"
#include "platform.h"
#include "seed.h"
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
inline thread_local u64 t_vcount =
    0;

// Debug check of the barrier protocol (see "Barrier protocol" above struct Run).
#if !defined(DDR5_CHECK_SYNC) && !defined(NDEBUG)
#define DDR5_CHECK_SYNC 1
#endif
#ifdef DDR5_CHECK_SYNC
inline thread_local u64 t_syncs = 0;
#endif

// Index of the first worker whose sync() count differs from worker 0's, or -1 if all are equal
inline int sync_count_mismatch(const std::vector<u64>& counts) {
    for (size_t i = 1; i < counts.size(); i++)
        if (counts[i] != counts[0])
            return (int)i;
    return -1;
}

// Where the tested memory is and how it is split between the worker threads.
struct Layout {
    u8* region = nullptr;
    std::atomic<u64> regionBytes{0};
    int T = 1; // worker threads
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

struct Run;
struct Settings;
int run_suite(const Settings& s, Run& R);

// The state of one run of the suite, in three parts: - the algo-facing part (public, first)
struct Run {
    // algo-facing: data (read-only for the algos)
    const Logger& log;
    explicit Run(const Logger& l) : log(l) {}
    Run(const Run&) = delete;
    Run& operator=(const Run&) = delete;
    u64 salt = 0;
    Rng rng = Rng::Default; // hash mixer chosen in Settings; see mix64()
    Layout layout;
    // Which pages of the region a finished verify has covered (coverage.h).
    CoverageMap cover;
    std::string testName; // the running test; also the key into winctr
    std::map<std::string, u64> winctr;
    // The first window counter of a test that has `njobs` jobs: job ji uses counter base + ji (see view()), and
    // the next call for the same test continues after them, so a repeated test moves on to the next windows.
    u64 claim_windows(u64 njobs) {
        const u64 base = winctr[testName];
        winctr[testName] += njobs;
        return base;
    }
    std::atomic<bool> stop{false};
    // algo-facing: methods call once per verified 64B line
    void vtick() {
        if (++t_vcount >= kTickLines)
            vflush();
    }
    // Records that `lines` lines starting at `base`
    void mark_verified(const u8* base, u64 lines) { cover.mark((u64)(base - layout.region), lines * kLineBytes); }
    void sync() {
#ifdef DDR5_CHECK_SYNC
        t_syncs++;
#endif
        bar->arrive_and_wait();
    }
    void set_label(const std::string& s) {
        std::lock_guard<std::mutex> l(errors.mu);
        label = s;
    }
    void tick() { progress.done.fetch_add(kTickLines * kLineBytes, std::memory_order_relaxed); }
    void add_done(u64 bytes) { progress.done.fetch_add(bytes, std::memory_order_relaxed); } // bytes moved, for GB/s
    // Window `ctr` of the thread's chunk.
    View view(int tid, u64 wl, u64 ctr) const {
        const Chunk& c = layout.chunks[tid];
        if (!wl || wl >= c.lines)
            return {c.base, c.g, c.lines};
        u64 nw = (c.lines + wl - 1) / wl, s = (ctr % nw) * wl;
        if (s + wl > c.lines)
            s = c.lines - wl;
        return {c.base + s * kLineBytes, c.g + s, wl};
    }
    // A window of the same size that never overlaps view(tid, wl, ctr): where the disturb traffic goes.
    View dview(int tid, u64 wl, u64 ctr) const {
        const Chunk& c = layout.chunks[tid];
        View v = view(tid, wl, ctr);
        u64 sv = (u64)(v.base - c.base) / kLineBytes, sd = sv + wl;
        if (sd + wl > c.lines)
            sd = sv >= wl ? sv - wl : sv + wl;
        return {c.base + sd * kLineBytes, c.g + sd, wl};
    }
    u64 window_lines(const Params& P) const {
        u64 mb = P.num("window_mb", 0);
        if (!mb)
            return 0;
        u64 wl = (mb << 20) / kLineBytes / layout.T;
        wl = wl / kWindowGranuleLines * kWindowGranuleLines;
        return std::max<u64>(wl, kWindowGranuleLines);
    }
    void sleep_ms(u64 ms) {
        double end = plat::now() + ms / 1000.0;
        while (plat::now() < end && !stop.load())
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    template <class F>
    void parallel(F fn);
    void report(int tid, const u8* addr, const Line& e, const Line& a, const char* phase, bool reread = true);
    // Appends to errors.logf (CLI; no-op without one) and flushes.
    void write_error_log(const std::string& text);

    // front-end: follow a run from another thread
    struct Status {
        double tStart;
        u64 regionBytes; // size of the tested region; 0 before it is allocated
        u64 vtotal;
        u64 pagesCovered;
        u64 pagesTotal; // whole pages in the region; 0 before it is allocated
        u64 errors; // bad lines so far
        std::string label; // the running test
    };
    Status status() const {
        Status s{progress.tStart.load(std::memory_order_acquire),
                 layout.regionBytes.load(std::memory_order_acquire),
                 progress.vtotal.load(),
                 0,
                 0,
                 errors.errTotal.load(),
                 {}};
        if (s.regionBytes) {
            s.pagesCovered = cover.pages_covered();
            s.pagesTotal = cover.pages_total();
        }
        std::lock_guard<std::mutex> l(errors.mu);
        s.label = label;
        return s;
    }
    u64 error_count() const { return errors.errTotal.load(); }

  private:
    friend struct SuiteDriver; // suite.cpp: the implementation of run_suite
    void vflush() {
        u64 c = t_vcount;
        t_vcount = 0;
        if (c) {
            progress.vbytes.fetch_add(c * kLineBytes, std::memory_order_relaxed);
            progress.vtotal.fetch_add(c * kLineBytes, std::memory_order_relaxed);
        }
    }

    Cfg cfg;
    Progress progress;
    ErrorSink errors;
    Stats st, total; // this test's / the whole run's error statistics
    std::barrier<>* bar = nullptr;
    std::atomic<bool> aborted{false};
    bool pinWarned = false;
    std::string label;
};
static_assert(!std::is_copy_constructible_v<Run> && !std::is_copy_assignable_v<Run> &&
                  !std::is_move_constructible_v<Run> && !std::is_move_assignable_v<Run>,
              "Run is shared by address between the workers, the barrier and the front-end");

template <class F>
void Run::parallel(F fn) {
    std::barrier<> b(layout.T);
    bar = &b;
    std::atomic<int> running{layout.T};
    std::atomic<int> unpinned{0}; // workers whose plat::pin() failed
    // Nothing that goes wrong in here may reach std::terminate (a joinable std::thread that is destroyed
    std::exception_ptr failure;
    std::mutex failMu;
    auto fail = [&] { // call only from a catch block
        {
            std::lock_guard<std::mutex> l(failMu);
            if (!failure)
                failure = std::current_exception();
        }
        aborted = true;
        stop = true;
    };
#ifdef DDR5_CHECK_SYNC
    std::vector<u64> syncs((size_t)layout.T, 0);
#endif
    std::vector<std::thread> th;
    int started = 0;
    try {
        th.reserve((size_t)layout.T);
        for (int t = 0; t < layout.T; t++) {
            th.emplace_back([&, t] {
#ifdef DDR5_CHECK_SYNC
                t_syncs = 0;
#endif
                try {
                    if (!plat::pin(t))
                        unpinned++;
                    fn(t);
                } catch (...) {
                    fail();
                }
#ifdef DDR5_CHECK_SYNC
                syncs[(size_t)t] = t_syncs;
#endif
                vflush();
                b.arrive_and_drop();
                running--;
            });
            started++;
        }
    } catch (...) {
        fail();
        for (int k = started; k < layout.T; k++) {
            b.arrive_and_drop();
            running--;
        }
    }
    double t0 = plat::now(), tl = t0, hb = t0;
    u64 dl = progress.done.load();
    try {
        while (running.load() > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            double t = plat::now();
            if (!stop.load() && layout.regionBytes) { // user-set stop criteria: coverage target or time limit
                // coverage = pages of the region that a finished verify has covered (coverage.h), not bytes verified.
                const u64 pgTotal = cover.pages_total();
                if (progress.stopCoverage && pgTotal &&
                    cover.pages_covered() * 100 >= progress.stopCoverage * pgTotal) {
                    progress.stopReason = "coverage target reached";
                    stop = true;
                } else if (progress.stopSeconds > 0 && progress.tStart > 0 &&
                           t - progress.tStart >= progress.stopSeconds) {
                    progress.stopReason = "time limit reached";
                    stop = true;
                }
            }
            if (t - tl >= 0.5) {
                u64 d = progress.done.load();
                double rate = (d - dl) / (t - tl) / 1e9;
                tl = t;
                dl = d;
                std::string lab;
                {
                    std::lock_guard<std::mutex> l(errors.mu);
                    lab = label;
                }
                char buf[220];
                snprintf(buf, sizeof buf, "  [%5.0fs] %-50.50s %5.1f GB/s  cov %5s%%  errors: %llu (this test %llu)",
                         t - t0, lab.c_str(), rate,
                         layout.regionBytes ? percent_text(cover.pages_covered(), cover.pages_total()).c_str() : "0.0",
                         (unsigned long long)errors.errTotal.load(), // run total: st is reset for every test
                         (unsigned long long)st.lines.load());
                if (log.console && log.tty) {
                    printf("\r%-*.*s", kStatusWidth, kStatusWidth, buf);
                    fflush(stdout);
                } else if (log.console && t - hb >= 30.0) {
                    hb = t;
                    printf("%s\n", buf);
                    fflush(stdout);
                }
            }
        }
    } catch (...) {
        fail();
    }
    for (auto& x : th)
        x.join();
    bar = nullptr;
#ifdef DDR5_CHECK_SYNC
    // Rule 1 of the barrier protocol.
    if (!failure && !stop.load()) {
        int bad = sync_count_mismatch(syncs);
        if (bad >= 0) {
            fprintf(stderr,
                    "internal error: barrier protocol violated in '%s': worker %d made %llu sync() calls, worker 0 "
                    "made %llu\n",
                    testName.c_str(), bad, (unsigned long long)syncs[(size_t)bad], (unsigned long long)syncs[0]);
            fflush(stderr);
            std::abort();
        }
    }
#endif
    log.print_clear();
    if (unpinned.load() > 0 && !pinWarned) { // all workers are joined here, so logging needs no lock
        pinWarned = true;
        log.emitf("warning: %d of %d threads could not be pinned to a CPU, so the OS may move them between cores "
                  "(the results are still valid)\n",
                  unpinned.load(), layout.T);
    }
    if (failure)
        std::rethrow_exception(failure);
}

// The loop over the jobs of a test, for the algos that have a job list. Call it from inside R.parallel: every
// thread runs it with the same `njobs`. Per job: stop check, body(ji), then one sync() so that no thread starts
// the next job (or leaves the test) before all threads have finished this one. The body is everything of the
// job EXCEPT that last sync() (barrier protocol, rule 1: every thread makes the same number of sync() calls).
template <class F>
inline void for_jobs(Run& R, size_t njobs, F&& body) {
    for (size_t ji = 0; ji < njobs; ji++) {
        if (R.stop)
            break;
        body(ji);
        R.sync();
    }
}

static_assert((kTickLines & (kTickLines - 1)) == 0, "walk_n polls Stop with a mask");
// f(q) for q = 0..n-1, the plain sweep. After every kTickLines steps it reports progress (R.tick()) and ends
// early if Stop is raised, so a Stop is noticed within kTickLines steps, not at the very next one; fewer steps
// than that never poll, the caller checks Stop before and after (as `if (!R.stop) R.mark_verified(...)` does).
template <class F>
inline void walk_n(Run& R, u64 n, F&& f) {
    for (u64 q = 0; q < n; q++) {
        f(q);
        if (((q + 1) & (kTickLines - 1)) == 0) {
            R.tick();
            if (R.stop.load(std::memory_order_relaxed))
                return;
        }
    }
}

// walk q = 0..n-1 through an order; f(lineIndex)
template <class F>
inline void walk(Run& R, const Order& o, bool down, F&& f) {
    walk_n(R, o.n, [&](u64 q) { f(o.pos(q, down)); });
}

// walk_n for steps that are one 4 KiB page each (pagemix): Stop is polled BEFORE every step, so it ends the loop
// at the next page boundary (B1: a Stop must not wait for thousands of pages); progress is reported every
// kTickLines lines' worth of pages, the same amount per R.tick() as walk_n.
constexpr u64 kPagesPerTick = kTickLines / (u64)kLinesPerPage;
static_assert((kPagesPerTick & (kPagesPerTick - 1)) == 0, "walk_pages ticks with a mask");
template <class F>
inline void walk_pages(Run& R, u64 n, F&& f) {
    for (u64 q = 0; q < n && !R.stop.load(std::memory_order_relaxed); q++) {
        f(q);
        if (((q + 1) & (kPagesPerTick - 1)) == 0)
            R.tick();
    }
}

} // namespace ddr5
