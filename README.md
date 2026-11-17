# DDR5 RAM Test

C++20 / CMake memory tester for DDR5. Win32 GUI + console tool from one engine; console tool also builds on Linux. No installer, no config files; the suite is compiled in.

## Requirements

- AVX2 CPU + OS with YMM state. Checked first (`src/cpu_check.cpp`, built without AVX2): GUI shows a message box, CLI prints reason and exits 2.

## Use

- Run `ddr5ramtest.exe` - one copy at a time (named mutex); a second copy focuses the first.
- Threads - default = allowed logical CPUs (honours affinity / job limits); thread N pinned to N-th CPU, wraps.
- Megabytes - default ~90% of free RAM (min 512 MB left free); override capped at 97% and at commit limit; log says if reduced.
- Allocation retry - on failure retries 90%, 80% ... 20% of plan, logs `testing X GiB instead of Y GiB planned`; exits 2 only if all fail.
- Start / Stop - runs until Stop. Stop is a hard stop (no false errors; unverified pages are skipped).
- Stop on error - GUI default on; CLI default off.
- Coverage - % of 4 KiB virtual pages fully verified in the tested region; `--coverage N` stops at N%. "Passes" counts bytes verified.
- Log - `ddr5ramtest.log` next to exe (or `%TEMP%`); CLI `ddr5test.log` or `--log FILE`. Error line: addr, region offset, expected/actual, DQ lane/beat, x8 ODECC codeword guess, transient/stable/unstable.
- Exit codes (CLI) - 0 clean, 1 errors, 2 bad option / could not start, 3 internal error.
- Ctrl-C (CLI) - stops cleanly with summary; second Ctrl-C kills.
- GUI close/Stop - never blocks the window; after 3 s Stop becomes **Force quit**.
- `--test-seconds N` - caps timed tests. Piped output prints a progress line every 30 s.
- Seed - random per run, printed first, in log. `--seed 0x...` + same `-mb` / `-t` repeats data and order (not thread timing or physical placement).

## Advanced

- CPU cache - Default / Bypass (flush every access) / Cached / Non-temporal.
- `--flush auto|clflush|clflushopt` - auto uses CLFLUSHOPT when present.
- RNG - generator for hash tests.
- Test set - Full (28) or Quick (12).
- Stress FPU - AVX load on every thread.

## What is tested

28 tests built around DDR5 behaviour (BL16 bursts, bank groups, row open/close, bus turnaround, idle gaps, DQ crosstalk, address decode, pointer chase, read/write mixes). Full list in `src/suite_data.cpp`.

From stressapptest (reimplemented, no code copied):
- Pattern table - walking/alternating/busy patterns, 1-8 word repeat, inverted.
- Page shuffle - 4 KB pages copied, inverted, refilled, then all verified.
- Cache coherency - threads share lines, sums checked; tests caches, not DRAM.

Not from stressapptest:
- Evolving data - each line updates every 1-8 passes with a new generation; every line checked against its exact expected generation (finds lost / stray writes).

## Honest limits

- No access to DDR5 timings, voltage, temperature or physical addresses.
- On-die ECC can hide single-bit errors.
- Lane / beat / codeword attribution is a hint only (scrambling, lane swaps invisible).
- Coverage - virtual pages, not physical.
- Checked on Linux and CI only, not on real DDR5 hardware.

## Self-test

```
tools/selftest.sh                         # Linux, g++ 11+ AVX2, a few minutes
ctest --output-on-failure                 # unit_tests, then selftest.sh (label "slow")
SAN=address,undefined tools/selftest.sh   # under ASan + UBSan
SAN=thread tools/selftest.sh              # under TSan (rwstorm skipped)
```

- Builds engine normally and with `-DDDR5TEST_INJECT` (plants faults between write and verify).
- Checks orders, windows, hash mixers, seed reproduction, clean runs, bad options, coverage, AVX2 gate, injected faults found exactly, stop at every phase (SIGINT, exit within `STOPWAIT` s), `TMO` timeout per run.
- `tools/inject_checks.sh BIN` - portable injection checks; Windows CI runs them on the MSVC build.

## Build

```
build.bat                                   # MSVC, x64 Native Tools prompt -> build\ddr5ramtest.exe, ddr5test_cli.exe
cmake -S . -B build/cmake -G Ninja          # MinGW / Linux (CLI only)
cmake --build build/cmake
```

GitHub Actions builds both on `windows-latest`.

## Changing the suite

Edit `src/suite_data.cpp` and rebuild; selftest fails on misspelled keys.

## Layout

```
src/types.h            u8..u64, 64-byte Line, bit helpers
src/platform*.h/.cpp   OS layer (plat::), win / posix
src/log.*              Logger
src/params.*           string helpers, Params, TestDef
src/seed.*             run seed, hash mixer
src/order.*            address orders
src/gen.*              data generators
src/lineio.h           64-byte store / load / verify
src/run.*              Run, walk, error report
src/coverage.h         page coverage map
src/sizing.h           memory sizing / retry plan
src/flush.*            CLFLUSH / CLFLUSHOPT selection
src/cpu_check.*        AVX2 runtime gate
src/cpu_set.h          allowed-CPU set, pinning
src/algo.*, algo_*.cpp shared helpers, algo registry, one file per algorithm
src/suite.*            run_suite, failure summary, FPU stress
src/suite_data.cpp     the 28 tests
src/inject.*           fault-injection hooks (self-test only)
src/main_gui.cpp       Win32 GUI
src/main_cli.cpp       console front-end
src/stop_gate.h        GUI Stop / close rules
src/start_guard.h      guarded GUI run start
src/build_version.h.in version template (git describe)
src/app.rc, app.manifest  version resource, visual styles, DPI
tools/unit_tests.cpp   unit tests
tools/selftest.sh      unit + clean + injection runs
tools/inject_checks.sh portable injection checks
tools/format.sh        clang-format (--check in CI)
```
