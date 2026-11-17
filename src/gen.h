#pragma once
// gen.h - data generators: stressapptest-style pattern table, Gen, Var, build_vars.
#include <array>
#include <string>
#include <vector>
#include "log.h"
#include "params.h"
#include "seed.h"
#include "types.h"

namespace ddr5 {

// data generators (pure functions of line index)
using Beats = std::array<u32, 16>;
// Data patterns in the style of Google's stressapptest table
struct SatPat {
    const char* name;
    std::array<u32, 64> w;
    int len;
    int weight[4];
};
const std::vector<SatPat>& sat_table();
// Every table length is a power of two (1, 2, 4 or 64; sat_table() aborts otherwise)
static inline u32 sat_word(const SatPat& p, u64 idx, int rep) {
    return p.w[(idx >> rep) & (u64)(p.len - 1)];
}

struct Gen {
    int kind = 0;
    const SatPat* sp = nullptr;
    int rep = 0;
    Line pat{}, inv{};
    u64 seed = 0;
    Rng rng = Rng::Default; // mixer of the hash data (kind 1)
    int altshift = -1;
    u64 modn = 0, modoff = 0; // modulo-N: every Nth line (offset) is inverted
    void set_beats(const Beats& b) {
        for (int j = 0; j < 8; j++) {
            pat.w[j] = (u64)b[2 * j] | ((u64)b[2 * j + 1] << 32);
            inv.w[j] = ~pat.w[j];
        }
    }
    inline void make(u64 g, const void* addr, bool invert, Line& o) const {
        if (kind == 0) {
            bool s = invert;
            if (altshift >= 0)
                s ^= (bool)((g >> altshift) & 1);
            if (modn)
                s ^= (g % modn) == modoff;
            o = s ? inv : pat;
        } else if (kind == 3) {
            for (int j = 0; j < 8; j++) {
                u64 v = (u64)sat_word(*sp, g * 16 + 2 * j, rep) | ((u64)sat_word(*sp, g * 16 + 2 * j + 1, rep) << 32);
                o.w[j] = invert ? ~v : v;
            }
        } else if (kind == 1) {
            for (int j = 0; j < 8; j++) {
                u64 v = mix64(((g << 3) + j) ^ seed, rng);
                o.w[j] = invert ? ~v : v;
            }
        } else {
            for (int j = 0; j < 8; j++) {
                u64 v = ((u64)(uintptr_t)addr + 8 * j) ^ seed;
                o.w[j] = invert ? ~v : v;
            }
        }
    }
};
struct Var {
    std::string label;
    Gen g;
};

std::vector<Var> build_vars(const Params& P, u64 salt, Rng rng, const Logger& log); // salt, rng: Run::salt, Run::rng

} // namespace ddr5
