// inject.cpp - the planted faults behind the DDR5_INJECT hooks (see inject.h).
#include "inject.h"

#ifndef DDR5TEST_INJECT
#error "inject.cpp belongs only in the fault-injection engine (ddr5engine_inject, -DDDR5TEST_INJECT)"
#endif

#include "algo.h"
#include "lineio.h"
#include "run.h"
#include <atomic>
#include <cstdlib>
#include <stdexcept>

namespace ddr5::inject {

// XOR `mask` into byte `off` of line number `line`
static void flip(u8* base, u64 line, int off, u8 mask) {
    u8* c = base + line * kLineBytes + off;
    *c ^= mask;
    _mm_clflush(c);
}

// self-test only: corrupt memory between write and verify (algo_fill, after the first sync)
void after_fill(Run& R, int tid, size_t ji, const View& w) {
    if (tid == 0 && ji == 0) {
        u8* c = w.base + 640 * 64 + 20;
        c[0] ^= 0x05;
        c[1] ^= 0x10;
        w.base[900 * 64 + 4] ^= 0x80;
        for (int k = 0; k < 40; k++)
            w.base[(4096 + k) * 64 + 8] ^= 1;
        _mm_clflush(c);
        _mm_mfence();
    }
    R.sync();
}

// self-test only (march, first variant, after its first element = the initial write)
void march_after_element(int tid, size_t ji, size_t ei, const View& w) {
    if (tid == 0 && ji == 0 && ei == 0 && w.n > 8192) {
        flip(w.base, 700, 8, 0x01);
        flip(w.base, 701, 33, 0x15);
        flip(w.base, 5000, 63, 0x80);
        _mm_mfence();
    }
}

// self-test only (chase, 32 chains of the first seed, after the lines are written)
void chase_after_fill(int tid, u64 sd, u64 K, const View& w, const Order& o, u64 steps) {
    if (tid == 0 && sd == 0 && K == 32 && steps > 64) {
        flip(w.base, o.at(steps / 2), 40, 0x10); // chain 0 covers order positions [0, steps)
        flip(w.base, o.at(steps / 2 + 3), 0, 0x01); // chain 0 again, a few lines on
        flip(w.base, o.at(steps + 7), 0, 0x01); // chain 1 covers [steps, 2 * steps)
        _mm_mfence();
    }
}

// self-test only (copy, first method, after the copy and before the verify)
void copy_after_copy(int tid, size_t ji, u8* A, u8* B, u64 nA) {
    if (tid == 0 && ji == 0 && nA > 100000) {
        flip(B, 300, 12, 0x02);
        flip(B, 90000, 5, 0x40);
        flip(B, 90000, 50, 0x04);
        flip(A, 12, 40, 0x08);
        _mm_mfence();
    }
}

// self-test only (rwstorm, first job, after the passes and before the final check of each thread's
void rwstorm_before_final(int tid, size_t ji, const View& w) {
    if (tid == 0 && ji == 0 && w.n > 8192) {
        flip(w.base, 640, 20, 0x05);
        flip(w.base, 4097, 8, 0x01);
        _mm_mfence();
    }
}

// self-test only (rwstorm, first job): one read-side fault, once.
void rwstorm_read_fault(int tid, size_t ji, u64 i, Line& e) {
    static std::atomic<bool> fired{false};
    if (tid == 1 && ji == 0 && i == 1234 && !fired.exchange(true))
        e.w[2] ^= 0x100;
}

// self-test only, last pass
void evolve_last_pass(Run& R, int tid, size_t ji, const View& w, u64 p, u64 passes, u64 seed, u64 maxp, StoreFn store) {
    if (p == passes) {
        if (tid == 0 && ji == 0) {
            bool lost = false, flip = false;
            for (u64 i = 0; i < w.n && !(lost && flip); i++) {
                u64 g = w.g + i, per = evo_period(g, seed, maxp);
                u8* a = w.base + i * 64;
                if (!lost && p % per == 0) {
                    Line old;
                    evo_make(g, p / per - 1, seed, R.rng, old);
                    store(a, old);
                    lost = true;
                } else if (!flip && p % per != 0) {
                    a[8] ^= 0x20;
                    _mm_clflush(a);
                    flip = true;
                }
            }
            _mm_sfence();
            _mm_mfence();
        }
        R.sync();
    }
}

// self-test only: with DDR5TEST_THROW_IN_WORKER set, worker 1 throws between write and verify
void after_fill_throw(int tid) {
    if (tid == 1 && getenv("DDR5TEST_THROW_IN_WORKER"))
        throw std::runtime_error("injected worker failure");
}

// self-test only (algo_fill, after the last job): with DDR5TEST_EXTRA_SYNC set
void extra_sync(Run& R, int tid) {
    if (tid == 0 && getenv("DDR5TEST_EXTRA_SYNC"))
        R.sync();
}

// self-test only: with DDR5TEST_STOP_IN_EVOLVE set, raise stop at the start of rewrite pass 2
void evolve_stop_in_write(Run& R, int tid, u64 p) {
    if (tid == 0 && p == 2 && getenv("DDR5TEST_STOP_IN_EVOLVE"))
        R.stop = true;
}

// self-test only: with DDR5TEST_STOP_IN_FILL set, stop in the middle of the fill phase
void pagemix_fill_stop(Run& R, int tid, u64 pg) {
    if (tid == 0 && pg >= 1000 && getenv("DDR5TEST_STOP_IN_FILL"))
        R.stop = true;
}

// self-test only: with DDR5TEST_STOP_IN_COPY_FILL set
void copy_fill_stop(Run& R, int tid, u64 i) {
    if (i == 1000 && tid == 0 && getenv("DDR5TEST_STOP_IN_COPY_FILL"))
        R.stop = true;
}

// self-test only: one planted bit flip before the final verify of all pages
void pagemix_flip(Run& R, int tid) {
    if (tid == 0) {
        R.layout.region[3 * 4096 + 100] ^= 0x04;
        _mm_clflush(R.layout.region + 3 * 4096 + 64);
        _mm_mfence();
    }
    R.sync();
}

// self-test only: called by every thread after each page of the pagemix final verify.
void pagemix_final_stop(Run& R, int tid, u64 pg) {
    static bool done = false; // only thread 0 touches it
    if (tid != 0 || done || pg < 1000 || !getenv("DDR5TEST_FINAL_LATE_FLIP"))
        return;
    done = true;
    u8* last = R.layout.region + (R.layout.regionBytes / kPageBytes - 1) * kPageBytes;
    last[100] ^= 0x04;
    _mm_clflush(last + 64);
    _mm_mfence();
    if (getenv("DDR5TEST_STOP_IN_FINAL"))
        R.stop = true;
}

// self-test only: one lost/extra increment
void coherency_extra(u8* mem, int tid) {
    if (tid == 0) {
        static bool once = false;
        if (!once) {
            once = true;
            mem[0]++;
        }
    }
}

} // namespace ddr5::inject
