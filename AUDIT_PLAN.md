# DDR5 RAM Test: audit plan

Repo `jamesonbottle/3`, branch `jamesonbottle-patch-1`, validated at `f108410ebc4be95e7050bc9b4816b3c24424591c` on 2026-11-13.

## Resume

- Pick the first unchecked item in **Suggested order**.
- Run `bash tools/selftest.sh` (g++ 11+, AVX2) or `unit_tests` + injection checks first.
- When done - tick box, add Session log line. Dropped items `[~]` with reason.
- **WIN** - only verifiable on Windows/MSVC.
- Legend - `[ ]` open, `[x]` done, `[~]` dropped. P0 first. Verdicts CONFIRMED / MEASURED / PARTLY / REFUTED / UNMEASURED.

Prompt: > Read AUDIT_PLAN.md. Continue with the first unchecked item in "Suggested order". Baseline checks, change, selftest, update checkboxes and log.

## Validation

- Method - read all cited code, built Linux engine (0 warnings), unit tests pass; measurements on 2-vCPU KVM guest, nothing run on Windows/MSVC.
- No Windows detection proof - CONFIRMED
- Missing stop polls - CONFIRMED
- GUI Stop / close blocks - CONFIRMED
- No sleep inhibition - CONFIRMED
- Log write failures ignored - CONFIRMED
- Virtual address only - CONFIRMED
- Coverage not address coverage - CONFIRMED
- `--cache` ignored for evolve - CONFIRMED
- No AVX2 runtime check - CONFIRMED
- Single instance, `start_run` throw, `ncpu`, `pin` - CONFIRMED
- `g_sink` shared atomic - CONFIRMED, UNMEASURED
- `sat_word` `%` - CONFIRMED+MEASURED (2.3 -> 4.9 GB/s cached)
- Evolve divisions - CONFIRMED, UNMEASURED
- `g % modn`, coherency `x % L` - CONFIRMED, low value
- Chase misses MLP - CONFIRMED+MEASURED (per-hop CLFLUSH)
- FPU stress saturates - CONFIRMED (~8e8 iterations)
- Memory sizing / no retry - CONFIRMED
- memcpy dst not flushed - CONFIRMED
- `probe` reads unwritten memory - REFUTED
- Barrier protocol implicit - CONFIRMED, no deadlock found
- Skeleton repeated, run.h mixed, dead code - CONFIRMED
- Build hygiene - CONFIRMED

New findings:
- N1 - CLFLUSH serialization dominates default tests (0.8 vs 5.7 GB/s with CLFLUSHOPT, VM). See D1.
- N2 - pagemix needs 68 templates (~272 KB), not ~136 x 4 KB. Harmless.
- N3 - chase stopped a chain at first bad line. FIXED 2026-11-13.
- N4 - rwstorm concurrent verify had no deterministic injection. FIXED 2026-11-13 (`rwstorm_read_fault`); one limit remains.

## Plan by category

### A. Detection proof and CI

- [x] **A1** - Windows CI runs `unit_tests` + `inject_checks.sh` on MSVC build. Verified CI run 43558481957.
- [x] **A2** - Injection hooks for march, chase, copy, rwstorm + exact-count selftest checks.

### B. Stop and shutdown responsiveness

- [x] **B1** - Stop polls in pagemix verify, copy, hammer, startup memset, alloc/lock slicing. Hard stop. Large-page VirtualAlloc not interruptible.
- [ ] **B2 (P0, WIN)** (Implemented 2026-11-14, compiled and unit-tested in CI, NOT yet checked by hand on Windows: tick it after the manual checklist below, see Session log.) GUI close and stop.
  - Files: `main_gui.cpp:504-510` (WM_CLOSE joins on the UI thread), `:287-295` (Stop button disabled until join).
  - Do: set stop and wait for `WM_RUN_DONE` before destroying; keep the window responsive; add a force-exit path (second click or timeout) like the CLI's second Ctrl-C.
  - Done when (manual checklist): closing during allocation, mid-test and during the final verify never shows "Not responding".

### C. Soak reliability and resources

- [ ] **C1 (P0, WIN)** (Implemented 2026-11-14, CI-verified up to the OS state; the sleep-timeout run below is still manual, see Session log.) Sleep inhibition. Add `plat::keep_awake(bool)`: Windows `SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED)`, POSIX no-op. Call it from the long-lived run thread (the state is per calling thread) at the start of `SuiteDriver::run` with a `ScopeExit` that clears it. Decide whether the display may sleep. Log one line when active. Done when: a run survives a short power-plan sleep timeout, and normal idle behaviour returns afterwards.
- [x] **C2** - Log write failures set `logFailed`, warn once, repeat in summary (GUI part untested).
- [ ] **C3 (P1, WIN)** (Implemented 2026-11-15, Linux end-to-end tested, CI-compiled and unit-tested on MSVC; NOT yet seen on a Windows machine under real memory pressure: tick it after the manual check in the Session log.) Memory headroom and allocation retry. Today (before this change): auto size = 90% of available (`suite.cpp:244`, `main_gui.cpp:183`), cap 97%, working-set min = size + 256 MB (`platform_win.cpp:113`), failure exits 2. Do: keep a fixed floor for OS/GUI, retry at smaller sizes with a log line, consider the commit limit. Done when: constrained-memory runs degrade gracefully instead of "Could not run".
- [x] **C4** - Error lines print `off=` region offset; selftest checks offsets.
- [ ] **C5 (P2, WIN)** (Implemented 2026-11-15, compiled in CI, NOT yet tried by hand on Windows: tick it after the manual check in the Session log.) Single-instance guard (named mutex in `WinMain`; second copy activates the first or shows a message).

### D. Stressor throughput (find the real limiter first)

Order matters: D1 first, because at flush=1 it masks everything below.

- [x] **D1** - `flush.h/.cpp` - CLFLUSHOPT when CPU has it (`--flush auto` default), `clflush` forces old. Measured on VM only.
- [x] **D2** - `sat_word` uses `& (len-1)` mask; `sat_table()` checks power of two. 2.1x at `--cache cached`.
- [x] **D3** - Evolve period / division caching. ~20% faster unflushed, ~3% at default flush.
- [x] **D4** - Chase does all K loads, then flush, then verify. ~2x hop rate at K >= 8.
- [x] **D5** - `disturb_traffic` uses local accumulator, publishes once per call. Benefit unmeasured.
- [x] **D6** - `FpuKernel` reseeds lanes every 1000 outer loops; threads unpinned, low priority.
- [ ] **D7 (P2)** Low-value: coherency `x % L` (`algo_coherency.cpp:58`), `g % modn` (`gen.h:52`, no built-in test uses `modulo`). Only if profiling shows it.

### E. Option semantics, metrics, startup checks

- [x] **E1** - `--cache` override keyed off `algo_keys()`, so evolve gets it; `test_cache_override`.
- [ ] **E2 (P1)** Coverage semantics: **DECIDED 2026-11-15 by the owner: coverage must show real addresses checked** (not a byte counter), so this is implemented, not relabelled. Too large for one session, so it is split into E2a-E2f below; **tick E2 only when E2f is done**, and do the parts in order (each one leaves the build green and the old behaviour working until E2d switches it).
  - What "real address" means here: a virtual address inside the tested region, tracked at 4 KiB page granularity (`kPageBytes`): 64 GiB = 16 Mi pages = 2 MiB of bits. Physical addresses are not visible to user mode (and the OS may remap pages), so they are out of scope; say so in the README (E2e). A page counts as covered only when ALL of its 64 lines were compared by a verify that ran to the end (a stopped or cut-short job marks nothing, so a stop can never inflate coverage).
  - Facts found while splitting (code read 2026-11-15, nothing changed): the verified window is not always the written one: `algo_fill.cpp:88` and `algo_evolve.cpp:72` verify `vw` (the next thread's window when `cross` is set) and `algo_rwstorm.cpp:64` verifies the partner's view `pv`, so the marking must use the verified view. `algo_pagemix.cpp` verifies by page in `pm_check` (final verify) and `pm_copy` (source page, mid-run). `algo_coherency.cpp` and `algo_probe.cpp` never call `vtick()`, so they add nothing to today's counter either. Windows are multiples of `kWindowGranuleLines` = 4096 lines = 256 KiB (page aligned); the last window of a chunk is shifted back and overlaps (`Run::view`), so marking must be idempotent (a bit set twice counts once). Chunk edges and the region tail: check page alignment before relying on it (E2a).
  - [x] **E2a** - `coverage.h` - `CoverageMap`, atomic bit per 4 KiB page.
  - [x] **E2b** - `Run::mark_verified` hooked in fill, march, evolve, rwstorm, copy.
  - [x] **E2c** - Marks in pagemix, chase, hammer; coherency and probe decided.
  - [x] **E2d** - `--coverage` + stop criterion + CLI/GUI coverage use the map.
  - [ ] **E2e (P1, WIN)** (Partly done in E2d: live line, final summary and the GUI value use the map. Open: per-test line wording `covered X.X% of N pages (P passes)`, GUI label text, README, and the look on Windows.) Show it: CLI live line (`run.h:372-376`, `cov %`), per-test line and final summary (`suite.cpp:436-470`): print `covered X.X% of N pages (P passes)`; GUI "Coverage:" label and value (`main_gui.cpp:183`, `:477`) from `Status`; README (`README.md:13`, `:46`: replace "does not track which addresses were touched", state virtual addresses at 4 KiB and the all-64-lines rule, and that a Stop leaves unfinished jobs unmarked). GUI part cannot be built here: compile it in CI, tick only after a look on Windows (same convention as B2/C1).
  - [ ] **E2f (P1, Linux)** Prove the numbers are real addresses (the owner's requirement): an end-to-end audit that does not trust the map. A debug/inject-only hook records the offsets of lines actually compared (for example a second plain `std::vector<bool>` per line on a tiny region, filled from the verify paths under `DDR5TEST_INJECT`), and a selftest check compares it with the map after a full run, after a `--coverage 100` run, and after a SIGINT mid-run: the map may be a subset of the audit, never a superset. Plus a negative control: a hook that makes one algo skip one window must leave exactly that window's pages unmarked and `--coverage 100` must then NOT stop on its own. Done when: all three comparisons pass in `selftest.sh` (also under the sanitizer legs; skip under `SAN=thread` only if the audit vector itself races) and the mutation checks of E2a-E2c still fail as intended. Then tick E2f and E2.
- [x] **E3** - `cpu_check.cpp` AVX2 + YMM gate before any AVX2 code. CI-verified; real no-AVX2 CPU never seen.
- [x] **E4** - Bad option values validated in `bad_values()` with unit tests.

### F. Windows platform parity

- [x] **F1** - `ncpu()` / `pin()` honour affinity + job limits, wrap like Linux. CI-verified.
- [ ] **F2 (P2, WIN)** (Implemented 2026-11-15, compiled and unit-tested in CI at `6e6d309`, NOT yet tried by hand on Windows: tick it after the manual check in the Session log.) `start_run` exception safety (`main_gui.cpp:205-285`): `A.busy` and the UI are set before the thread is created (`:277-281`); wrap thread/`make_unique` creation in try/catch and restore state.

### G. Maintainability and hygiene

- [x] **G1** - Barrier rules documented in `run.h`; debug `DDR5_CHECK_SYNC` sync counter.
- [ ] **G2 (P2)** Share the job skeleton (jobs list, `winctr`, `parallel`, stop checks, sync) and move pagemix, copy, hammer and chase onto `walk`.
- [ ] **G3 (P2)** Split `run.h` (data model / orchestration / status printing) and break `Run::report` (`run.cpp:8-103`) into classify, format, write.
- [x] **G4** - `Run::report` uses `memcpy` into `u32[16]` + `static_assert`.
- [ ] **G5 (P2)** `hammer` and `probe` are unused by the built-in suite: either keep them as documented extras with tests or remove them.
- [x] **G6** - Version via CMake + git describe, /W4-clean MSVC, `.gitignore` log. CI-verified.
- [ ] **G7 (P2)** memcpy destination in `algo_copy.cpp:70-71` is not flushed before verify; flush it after the copy (affects only the cache-sized tail).

## Suggested order

1. **Session 1 (Linux, fully testable): safety net.** A2, E1, G4, G6d, C4. Everything later is protected by A2's new injection checks.
2. **Session 2 (Linux): stop/log/soak logic.** B1 (non-GUI parts), C2, G1, D5, D6.
3. **Session 3 (Linux, measurement-driven): throughput.** D1 -> D2 -> D3 -> D4. Re-run selftest after each step; get target-hardware numbers for D1 before changing the default.
4. **Session 4 (needs Windows / CI): platform items.** A1, B2, C1, C3, C5, E3, F1, F2, G6a-c.
5. **Next doable on Linux (P1): E2a -> E2b -> E2c -> E2d -> E2f, then E2e** (E2 is decided and split, see its sub-items; one or two sub-parts per session). B2, C1, C3, C5, F2 stay open only for their manual Windows checks.
6. **Later (P2):** G2, G3, G5, G7, D7 (E4 is done).

## Open decisions

1. ~~Pagemix verify after Stop - hard stop.~~ Decided.
2. ~~Coverage - real virtual-page coverage (E2).~~ Decided.
3. CLFLUSHOPT as default or as an option (D1); needs real-hardware numbers. **Current state (D1 session): `--flush auto` is the default, i.e. CLFLUSHOPT whenever the CPU reports it; `--flush clflush` forces the old behaviour.** To make plain CLFLUSH the default again, change `FlushMode flushMode = FlushMode::Auto` in `suite.h` (one line). Benchmark on Zen 4/5 and Raptor Lake before keeping auto.
4. ~~Sleep - display may turn off (`ES_SYSTEM_REQUIRED` only).~~ Decided.
5. ~~FPU threads - unpinned, low priority.~~ Decided.
6. ~~Windows `pin()` - wraps like Linux.~~ Decided.

## Dropped / downgraded

- `[~]` Audit Low item "probe reads never-written memory": refuted (see summary).
- Downgraded: `g_sink` scope (one built-in test), `g % modn` (unused), coherency `%` (not DRAM bound), silent bad values (developer-facing only).

## Evidence (reproduce with these)

Environment: KVM guest, Intel Xeon @ 2.1 GHz, 2 vCPU, 8 GB, g++ 13.3, CMake 3.28, Ninja. All runs `-t 2 --no-large-pages`.

```
cmake -S . -B $B/cmake -G Ninja -DDDR5_OUTPUT_DIR=$B/out
cmake --build $B/cmake --target unit_tests ddr5test_cli ddr5test_cli_inject
$B/out/unit_tests                                   # baseline: all checks passed
$B/out/ddr5test_cli -mb 128 -t 2 --no-large-pages -f "Solid and checker"   # add --cache cached for the no-flush runs
```

| Run | Result |
|---|---|
| "Solid and checker", default (flush=1, CLFLUSH) | 0.8 GB/s, 5.5 s |
| "Stressapptest-style patterns", default | 0.8 GB/s, 24.3 s |
| "Solid and checker", `--cache cached` | 8.6 GB/s, 0.5 s |
| "Stressapptest-style patterns", `--cache cached` | 2.3 GB/s, 8.0 s |
| same, `sat_word` mask patch (`gen.h:28`: `& (u64)(p.len - 1)`) | 4.9 GB/s, 3.8 s |
| "Solid and checker", default, CLFLUSHOPT build | 5.7 GB/s, 0.8 s |
| Planted-fault run (`ddr5test_cli_inject`, "Solid and checker"), CLFLUSHOPT build | exit 1, 42 lines / 44 bits (expected) |
| FPU kernel, `x=x*1.0000001f+1e-7f` | `m` = 1.0000001192; lane overflows after ~8.02e8 iterations (~40,100 outer loops of 20,000) |

Chase, `-mb 1024`, ns per hop (thread 0), K = 1, 2, 4, 8, 16, 32:

| Variant | ns/hop |
|---|---|
| Original loop | ~167, 134-141, 140, 135-139, 122-134, 131-136 (two seeds) |
| Loads batched first, CLFLUSH | ~173, 143-147, 180-192, 148-175, 144-153, 134-149 |
| Loads batched first, CLFLUSHOPT | 167, 105, 76, 50, 39, 38 |

Temporary patches used (scratch copy only, not committed): per-job timer `tc0` plus a `[probe]` print around the chain loop in `algo_chase.cpp`; for CLFLUSHOPT replace `_mm_clflush(` with `_mm_clflushopt(` in `lineio.h` and `algo_chase.cpp`, cast `(void*)a` in `IO::verify`, configure with `-DCMAKE_CXX_FLAGS=-mclflushopt`.

## Session log

| Date | Session | What was done |
|---|---|---|
| 2026-11-13 | Validation | Validated audit against `f108410`, measured on Linux VM, wrote plan. |
| 2026-11-13 | Session 1: A2 | Hooks march/chase/copy/rwstorm + 4 checks. |
| 2026-11-13 | Session 1: A2 follow-up | Fixed N3 (chase continues past bad line) and N4 (`rwstorm_read_fault` hook). |
| 2026-11-13 | Session 1: E1 | `--cache` override via `algo_keys()`; `test_cache_override`. |
| 2026-11-13 | Session 1: G4 | `memcpy` in `Run::report`. |
| 2026-11-13 | Session 1: G6d | `ddr5test.log` in `.gitignore`. |
| 2026-11-13 | Session 1: C4 | Error lines print `off=`. |
| 2026-11-13 | Session 2: C2 | `Run::write_error_log`, `logFailed`, one warning. |
| 2026-11-13 | Session 2: B1 (non-GUI) | Stop checks in pagemix, copy, hammer, startup memset. |
| 2026-11-13 | Session 2: G1 | Barrier rules + `DDR5_CHECK_SYNC`. |
| 2026-11-13 | Session 2: D5 | `g_sink` per-thread accumulator. |
| 2026-11-13 | Session 2: D6 | `FpuKernel` with reseed. |
| 2026-11-14 | Session 3: D1 | `flush.h/.cpp`, `--flush` option. |
| 2026-11-14 | Session 3: D2 | `sat_word` mask. |
| 2026-11-14 | Session 3: D3 | Evolve period caching (mix64 not the limiter). |
| 2026-11-14 | Session 3: D4 | Chase batches K loads. |
| 2026-11-14 | B1 (lock slicing) | `plat::alloc` takes stop flag, slices first touch. |
| 2026-11-14 | A1 (Windows CI) | `inject_checks.sh` + MSVC CI steps. |
| 2026-11-14 | A1 verification | CI `ff42f72` green; 14 PASS on MSVC. |
| 2026-11-14 | B2 (GUI close / stop) | Baseline green at `9699da3` (`unit_tests` pass, 0 build warnings). B2 was the first unchecked item of "Suggested order" after A1. **Cause:** `WM_CLOSE` joined the worker on the UI thread (any stop that takes seconds = "Not responding"), and the Stop button stayed disabled until the run ended with no way out. **Change:** new `src/stop_gate.h` (`StopGate`, pure logic, header only): Stop and close only ask the run to stop; the window is destroyed when `WM_RUN_DONE` arrives; after 3 s of stopping the Stop button turns into "Force quit" and a second close click does the same (`force_exit()`: log closed best-effort via `try_lock`, then `TerminateProcess`, not `ExitProcess`, because the static `App` would destroy a joinable `std::thread`; same idea as the CLI's second Ctrl-C). A second click inside the 3 s is ignored, so a double click cannot kill the program. No automatic timeout kill (decision: only the user ends a stuck stop). `main_gui.cpp`: `apply_gate`, `stopping_text()` (the status line used to be overwritten with the test name every 500 ms while stopping), `on_run_done` no longer shows the "could not start" box while the window is closing, `refresh()` offers Force quit only while the run is going (a first version could have re-offered it after the run ended; caught while reviewing). README updated. Tests: unit `test_stop_gate` (idle close, double click ignored, force only after the delay, offer reported once, close waits for done, Stop-then-close, close after the run ended, new run starts clean, delay counted from the stop request). Mutation check on a scratch copy (second click forces without the delay): 5 checks fail. Full `tools/selftest.sh`: 111 PASS, 0 FAIL, 0 SKIP; `format.sh --check` clean. CI at `36a2234`: all four jobs green; in the `windows` job `main_gui.cpp` compiled under MSVC /W4 with no warning from the new code (only the old `_wfopen` C4996 at its fopen lines), `unit_tests` Passed (including `test_stop_gate`), injection checks PASSED. **Manual checklist (not done, needs a Windows machine with the GUI):** (1) Start, click Stop at once during "Allocating memory..." with a large size: the window keeps repainting, "Stopping..." shows, the button becomes "Force quit" after ~3 s if the allocation is still running. (2) Close the window during allocation, mid-test and during the page shuffle's final verify: no "Not responding"; the window closes by itself when the run ends. (3) Click close twice quickly: nothing happens; after 3 s a second close (or Force quit) ends the program at once, and the log has every error reported so far. **Limits:** (1) the window behaviour itself was not seen on any machine (no GUI test harness; the Windows runner builds but does not drive the GUI); only the decision logic is tested. (2) `TerminateProcess` leaves the log handle to the OS if the worker held the log mutex. (3) A large-page `VirtualAlloc` still cannot be interrupted, so Stop can wait for it, now with the UI alive and the Force quit way out. (4) A local MSVC/MinGW syntax check was not possible here (an install of a cross compiler was refused), so CI was the first compile of `main_gui.cpp`. Next: tick B2 after the manual checklist; then C1, C3, C5, E3, F1, F2, G6a-c. |
| 2026-11-14 | C1 (sleep inhibition) | Baseline green at `4b2f120` (`unit_tests` pass, 0 build warnings). C1 was the first unchecked item of "Suggested order" that can be done without a hands-on GUI check (B2's manual checklist is still open). New `plat::keep_awake(bool)` (`platform.h`, `platform_win.cpp`, `platform_posix.cpp`) returning `KeepAwake::{NotSupported, On, Failed}`: Windows `SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED)` and, to clear, `ES_CONTINUOUS`; POSIX does nothing. `SuiteDriver::run` (`suite.cpp`) takes it right after the error log is open and before the long sizing / allocation, on the thread that runs the test (the OS ties the request to the calling thread; CLI = main thread, GUI = the worker), and clears it with a `ScopeExit`. One log line when active ("keeping the system awake while the test runs (the display may still turn off)"), a warning if Windows refuses, nothing on Linux. Decision (Open decision 4): the display may still sleep. Does NOT stop a user-initiated sleep (power button, lid, Start menu) or a forced one (low battery, policy). Tests: unit `test_keep_awake` (Linux: `NotSupported`; Windows: after `keep_awake(true)` a probe `SetThreadExecutionState` reports `ES_SYSTEM_REQUIRED`, after `keep_awake(false)` it does not), and the Windows smoke step now captures the quick-suite output and requires the line. Full `tools/selftest.sh`: 111 PASS, 0 FAIL, 0 SKIP; `format.sh --check` clean. CI at `aa67265`: all four jobs green; in the `windows` job `unit_tests` Passed (so the thread-state probe agrees with the code) and the log shows the "keeping the system awake" line in both smoke runs. **Manual check still open (needs a Windows machine):** set the power plan's sleep timer to 1-2 minutes, start a run, leave the machine untouched for longer than that: it must not sleep and the run must reach its end; after the run (or Stop) the idle timer applies again. **Limits:** (1) the effect on a real power plan was not observed: the runner proves the OS accepted the request and holds it, not that a sleep timer is then ignored; Modern Standby (S0ix) machines may behave differently, which is exactly what the manual check should cover. (2) The GUI path has the same call but was not run. (3) Nothing mutation-tested: the Linux build cannot exercise the Windows branch. Next: tick C1 and B2 after their manual checks; then C3, C5, E3, F1, F2, G6a-c. |
| 2026-11-15 | C3 (memory headroom, retry) | Baseline green at `3c2b111` (`unit_tests` pass, 0 build warnings). The first unchecked item of "Suggested order" is B2, but only its manual Windows GUI check is left (C1 is in the same state), so this session took the next one that can be verified without hands-on Windows work, C3; B2 and C1 stay open. New `src/sizing.h` (pure logic, header only): `plan(requested, avail, availCommit)`: automatic size = 90% of the free memory but at least 512 MB stay free (`kAutoFloor`); a requested size is capped at 97% leaving at least 256 MB (`kCapFloor`, chosen so that the working-set request of `size + 256 MB` in `platform_win.cpp` always fits in the free memory) and at the commit limit minus 256 MB. New `plat::avail_commit()`: Windows `ullAvailPageFile` (MS docs: what the current process can still commit), POSIX 0 = unknown (Linux overcommits, so a refused `mmap` is handled by the retry). `SuiteDriver::allocate` now goes through `sizing::alloc_with_retry`: a refused allocation is retried at 90%, 80%, ... 20% of the planned per-thread size (whole 2 MB pieces; steps that round to the same size are skipped), each retry logged (`allocation of N MB failed; trying M MB`); the run then continues on the size that worked and says `testing X GiB instead of the Y GiB planned (N allocation attempts)`; `per`, `total`, the log header and the coverage line follow the real size. If every size is refused: `allocation failed at every size from A MB down to B MB (9 attempts)`, exit 2. A raised Stop ends the retrying. The GUI "Megabytes" default (`auto_mb`) uses the same `plan()`. Log texts for the 97% cap are unchanged; a new one covers the commit cap. README: new "Memory size" paragraph. Tests: unit `test_sizing` (floors, caps, commit limit, unknown values, the `size + 256 MB <= free` invariant over six machine sizes, per-thread split, the retry sequence, retry loop with a fake allocator: success at once / after 3 refusals / gives up after 8 retries / stop / tiny sizes / zero); selftest section 2f (3 checks): under `ulimit -v` 768 MiB a 1 GiB request is refused, retried (1024 -> 920 -> 816 -> 716 MB here), runs on 0.70 GiB, exit 0, no error, log header and summary show the real size; under 256 MiB a 2 GiB request exits 2 with the 9-attempt message and starts no test. Section 2f is SKIPped under the sanitizers (they cannot run under an address-space limit), with under 3 GiB available, or when the tester cannot start under the limit. Full `tools/selftest.sh`: 114 PASS, 0 FAIL, 0 SKIP (was 111; 5m33s); `format.sh --check` clean. Mutation checks on a scratch copy: retries disabled -> 7 unit checks fail and the 768 MiB run exits 2; `kCapFloor` = 0 -> the unit check on the capped size fails. CI at `1ac0b8f` (push run 43682193602): all four jobs green; in the `windows` job the new code compiled under MSVC with no new warning (only the old C4324 / C4996 ones), `unit_tests` Passed (so `test_sizing` holds under MSVC), injection checks PASSED, and both smoke runs went through the new `plan()` and `avail_commit()` (`testing 0.06 GiB of 13.46 GiB free`, no retry needed). **Manual check still open (needs a Windows machine):** with little free memory (start a big allocation in another program, or run on a small VM with the page file off), start the GUI with automatic size, then again with a size above the free memory: the run must start, the status/log must show the reduction or the `trying` lines, and nothing may end in "Could not run" while a smaller size is possible; also check that Windows and the window stay responsive (the 512 MB floor is a guess). **Behaviour changes (reversible):** on machines with less than ~5 GB free the automatic size is now `free - 512 MB` (for example 2 GB free: 1.5 GiB, was 1.8 GiB); a requested size above `free - 256 MB` is cut a little earlier than the old 97% on machines with less than ~8 GB free; an explicit `-mb` / GUI size is also retried smaller (it used to fail); if you want a fixed size or nothing, that is one condition in `SuiteDriver::allocate`. **Limits:** (1) nothing was observed on Windows: `ullAvailPageFile` is used as documented, not measured, and the real "another program took the memory" case was only imitated on Linux with `RLIMIT_AS` (a refused `mmap`), which exercises the retry loop and the messages but not the Windows API. (2) Only a refused allocation call is retried. Memory that is committed but not really available (Windows pages it out during the first touch; Linux may OOM-kill) is not detected; the floors reduce that risk, nothing tests it. (3) A failed large-page `VirtualAlloc` still falls back to normal pages inside `plat::alloc` first; only then does a refusal shrink the size. (4) The 512 MB / 256 MB floors, the 10% step and the 20% minimum are my choices, not measured. (5) The `size + 256 MB <= free` unit check uses `kCapFloor` itself, so it cannot catch a wrong constant; the exact-value checks do (that is how the `kCapFloor = 0` mutant was caught). (6) The selftest limits (768 / 256 MiB, 1 / 2 GiB requests) assume a process baseline address space well below 256 MiB; the probe step SKIPs the group if the tester cannot start under it. Next: tick C3, C1 and B2 after their manual checks; then C5, E3, F1, F2, G6a-c. |
| 2026-11-15 | C5 (single instance) | Baseline green at `517ef29` (`unit_tests` pass, 0 build warnings). C5 is the next item of Session 4 after C3 (B2, C1 and C3 wait for manual Windows checks). `WinMain` (`main_gui.cpp`) now takes the named mutex `Global\\Ddr5RamTestSingleInstance` before anything else (`Global\\` so a copy in another user session counts too; the handle is kept until the process ends, so a crash releases it). A second copy calls `activate_running_copy()`: it looks for the window class `Ddr5RamTestWindow` (new constant `WND_CLASS`, also used to register the class), waiting up to 2 s because the first copy creates the mutex before its window, restores it when minimised, calls `SetForegroundWindow` and exits 0; with no window on this desktop it shows a message box ("already running (in another user session, or as another user)"). `ERROR_ACCESS_DENIED` from `CreateMutexW` (mutex owned by another security context, for example an elevated copy) is treated as "already running"; any other failure to create the mutex lets the program start unguarded. README step 1 says so. No unit test: the logic is two Win32 calls and a decision, nothing portable to test; the Linux `selftest.sh` does not build the GUI. Full `tools/selftest.sh`: 114 PASS, 0 FAIL, 0 SKIP (5m21s, unchanged); `format.sh --check` clean. CI at `17598c0` (push run 43683690511): all four jobs green; `main_gui.cpp` compiled under MSVC /W4 with only the two old `_wfopen` C4996 warnings; `unit_tests` Passed, injection checks PASSED. **Manual check still open (needs a Windows machine):** (1) start `ddr5ramtest.exe`, start it again: no second window, the first comes to the front (or its taskbar button flashes); (2) minimise the first, start a second copy: the first is restored; (3) start both within about a second of each other: still only one window; (4) close the first, start again: starts normally; (5) optional: first copy elevated, second not: the message box or the first window, never two copies. **Limits:** (1) nothing was observed on any machine; the Windows runner builds the GUI but does not drive it. (2) `SetForegroundWindow` can be refused by Windows (the first copy then only flashes in the taskbar); that is a Windows rule, not handled. (3) Only the GUI is guarded: the console tool, or a console run next to the GUI, is not (two memory tests at once are possible; they would each size from the free memory, which C3's retry softens but does not prevent). (4) The mutex name is fixed, so a renamed or second-copy-on-purpose setup (e.g. a copy per user with different logs) is no longer possible. Next: tick C5, C3, C1 and B2 after their manual checks; then E3, F1, F2, G6a-c. |
| 2026-11-15 | E3 (AVX2 runtime check) | `cpu_check.h/.cpp`, `avx2_decide`. |
| 2026-11-15 | F1 (Windows CPU set) | `cpu_set.h`, `ncpu()` / `pin()` follow affinity. |
| 2026-11-15 | F1 verification | CI `351efc6` green incl. affinity smoke step. |
| 2026-11-15 | F2 (GUI start_run) | Baseline green at `351efc6` (`unit_tests` pass, 0 build warnings). The first unchecked items of "Suggested order" (B2, C1, C3, C5) only wait for manual Windows checks, and F1 needed only its CI check (above), so this session took F2. **Cause:** `start_run` set `A.busy`, started the `StopGate` and switched the controls to "running" BEFORE creating the worker (`std::thread` throws `std::system_error` when the OS refuses a thread; `make_unique<Run>` can throw `bad_alloc`), and the log file was already open. A throw there left the window in "running" for good: no `WM_RUN_DONE` ever came, Start stayed disabled, the log handle stayed open. **Change:** new `src/start_guard.h` (`try_start(step, why)`: runs the step, returns false with the exception text, nothing escapes). `start_run` now builds the `Run` and the worker thread into locals inside `try_start`, and only when it returned true moves them into `A.run` / `A.worker` and switches the window (`A.busy`, `gate.on_run_start()`, `set_running_ui(true)`). The worker may already be running at that point; that is safe because this is the UI thread, so its `WM_RUN_DONE` is handled only after `start_run` has returned, and `A.worker` is never joinable at the assignment (joined at the top). On failure `abort_start(why)` writes `run did not start: <why>` to the log and closes it, sets the status to "Could not run" and shows a message box with the reason (ANSI text converted with `MultiByteToWideChar`); Start stays enabled, the previous `Run` is untouched. README: GUI sentence and file list. Tests: unit `test_start_guard` (success runs once and clears the reason; `system_error` text kept; `bad_alloc`; a non-std exception and an exception with an empty text both give "unknown error"; a later success clears the reason; the start_run pattern: a failed second step commits nothing; a successful step leaves a joinable worker running on the Run it built). Mutation check on a scratch copy (helper catches only `logic_error`): 2 checks fail. Full `tools/selftest.sh`: 117 PASS, 0 FAIL, 0 SKIP (5m20s, unchanged: the new unit checks are inside the one unit-test check); `format.sh --check` clean. CI at `6e6d309` (push run 43702888544): all four jobs green; in the `windows` job `main_gui.cpp` compiled under MSVC /W4 with only the two old `_wfopen` C4996 warnings (now at lines 284 and 293), `unit_tests` Passed (2.59 s, so `test_start_guard` holds under MSVC), injection checks PASSED. **Manual check still open (needs a Windows machine with the GUI):** the normal path changed order, so check it first: start `ddr5ramtest.exe`, click Start (status shows "Allocating memory..." then the test names, counters move), click Stop (it stops, Start works again), start a second run, close the window during it. The failure path (the OS refusing a thread) cannot be provoked without a debugger or a fault hook; none was built. **Limits:** (1) nothing was seen on a real window: the Windows runner builds the GUI but does not drive it, so the reordered normal path is reasoned about, not observed; (2) the rollback (log line, close, message box) was never run, only the helper under it is unit-tested; (3) only `make_unique<Run>` and the thread creation are guarded: the `std::wstring` work in `start_run` and the UI updates after the commit are not, and an exception there would end the program as before; (4) a failed attempt leaves its run header line in the log, followed by `run did not start`; (5) `main_gui.cpp` could not be compiled here (no MSVC), so CI was its first compile. Next: tick F2, B2, C1, C3, C5 after their manual checks; then G6a-c. |
| 2026-11-15 | G6 (build hygiene, verification) | Version / warnings already in `b4b5ac7`; verified. |
| 2026-11-15 | E4 (silent bad values, verification) | Already in HEAD; verified, no source change. |
| 2026-11-15 | E2 (coverage, split into E2a-E2f) | Baseline green at `f205a6b` (build 0 warnings, `unit_tests` pass). The first unchecked items of "Suggested order" (B2, C1, C3, C5, F2) only wait for manual Windows checks, so this session took E2, the first one that can be done on Linux. **Owner decision: coverage must show real addresses checked**, and E2 is too large for one session, so this session only edited the plan, no source change: E2 is now a parent with six sub-parts (E2a `CoverageMap` bitmap + unit tests; E2b hook the `Run::view` algos; E2c hook pagemix / chase / coherency-probe-hammer one at a time; E2d switch `--coverage` and the stop criterion; E2e CLI / GUI / README display; E2f end-to-end audit that the map is never a superset of what was really compared). Open decision 2 and "Suggested order" updated. Facts that shaped the split (from reading the code): fill, evolve and rwstorm verify another window than the one they wrote (`vw`, `pv`), pagemix verifies per page, coherency and probe never call `vtick()`, windows are 256 KiB multiples and the last one overlaps. Scope: virtual addresses at 4 KiB, a page counts only after a finished verify; physical addresses are out of scope. Full `tools/selftest.sh` run after the edit: 119 PASS, 0 FAIL, 0 SKIP (nothing but `AUDIT_PLAN.md` changed). Next: E2a. Not committed or pushed: ask first. |
| 2026-11-15 | E2a (CoverageMap) | `coverage.h` `CoverageMap`. |
| 2026-11-16 | E2b (mark verified pages in fill, march, evolve, rwstorm, copy) | `Run::mark_verified` in five algos. |
| 2026-11-16 | E2c (pagemix, chase, hammer hooked; coherency and probe decided) | Marks in pagemix, chase, hammer. |
| 2026-11-16 | E2d (switch `--coverage` and the stop criterion to the map) | Stop criterion and status use the map. |
