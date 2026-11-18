#pragma once
// suite.h - run settings and the run_suite() entry point used by both front-ends.
#include <string>
#include <vector>
#include "flush.h"
#include "params.h"
#include "run.h"
#include "seed.h"
#include "types.h"

namespace ddr5 {

// The enumerators are in the order of the CLI --cache / GUI "CPU cache" and "Test set" lists
enum class CacheMode : int {
    Default,     // per test
    Bypass,      // clflush every access
    Cached,      // plain cached stores
    NonTemporal, // non-temporal stores
};
enum class SuiteSet : int {
    Full,
    Quick, // no long pauses / hammer
};

struct Settings {
    u64 megabytes = 0;       // 0 = auto (90% of free memory)
    int threads = 0;         // 0 = all logical CPUs
    bool stopOnError = true; // beeping is done by the front-end
    CacheMode cacheMode = CacheMode::Default;
    FlushMode flushMode = FlushMode::Auto;
    Rng rngMode = Rng::Default; // hash mixer for 'hash' data; Default = SplitMix
    bool stressFpu = false;
    bool largePages = true;
    SuiteSet suite = SuiteSet::Full;
    std::string logFile = "ddr5test.log";
    bool logFileSet = false;
    u64 coverageStop = 0;
    double minutes = 0;
    u64 loops = 0;
    std::string filter;
    u64 testSeconds = 0;
    bool seedSet = false;
    u64 seed = 0;
};

std::vector<TestDef> builtin_suite();
// The tests a run with these settings executes: filters applied
std::vector<TestDef> select_tests(const Settings& s, std::vector<TestDef> tests);

// The arithmetic of one FPU stress thread (--fpu), exposed so the unit tests can run it.
constexpr int kFpuInner = 20000;
constexpr u64 kFpuReseedLoops = 1000;
struct FpuKernel {
    float v[8][8];
    u64 loops = 0; // outer loops since the last re-seed
    FpuKernel() { reseed(); }
    void reseed();
    void run(unsigned outer);
    float sink() const;
};
// Runs the built-in suite until finished or R.stop.
int run_suite(const Settings& s, Run& R);

} // namespace ddr5
