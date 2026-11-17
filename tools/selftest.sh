#!/usr/bin/env bash
# Reproducible self-test of the engine on Linux (needs g++ 11+ and an AVX2 CPU).
set -u
cd "$(dirname "$0")/.."
CXX=${CXX:-g++}
OUT=${OUT:-$(mktemp -d)}
mkdir -p "$OUT"
RUN="-mb 64 -t 2 --no-large-pages --test-seconds 3"
SAN=${SAN:-}
# every run of the tester is wrapped in timeout
if [ -n "$SAN" ]; then TMO=${TMO:-600}; else TMO=${TMO:-120}; fi
case "$SAN" in
  thread) export TSAN_OPTIONS="${TSAN_OPTIONS:-} exitcode=66 halt_on_error=0 history_size=4" ;;
  *address*) export ASAN_OPTIONS="${ASAN_OPTIONS:-} detect_leaks=1 abort_on_error=0 exitcode=66"
             export UBSAN_OPTIONS="${UBSAN_OPTIONS:-} print_stacktrace=1 halt_on_error=1" ;;
esac
fail=0
ok()   { printf '  PASS  %s\n' "$1"; }
bad()  { printf '  FAIL  %s\n' "$1"; fail=1; }
check() { if eval "$2"; then ok "$1"; else bad "$1"; fi; }

echo "== build ($CXX, CMake) -> $OUT"
# unit_tests, ddr5test_cli (normal engine) and ddr5test_cli_inject
cmake -S . -B "$OUT/cmake" -DCMAKE_CXX_COMPILER="$CXX" -DDDR5_OUTPUT_DIR="$OUT" -DDDR5_SANITIZE="$SAN" -DDDR5_CHECK_SYNC=ON > "$OUT/cmake.log" 2>&1 || { cat "$OUT/cmake.log"; exit 2; }
cmake --build "$OUT/cmake" --target unit_tests ddr5test_cli ddr5test_cli_inject -j "$(nproc 2>/dev/null || echo 2)" > "$OUT/build.log" 2>&1 || { cat "$OUT/build.log"; exit 2; }
grep -E "error|warning" "$OUT/build.log" && fail=1
mv -f "$OUT/ddr5test_cli" "$OUT/cli"; mv -f "$OUT/ddr5test_cli_inject" "$OUT/cli_inject"

echo "== 1. unit tests"
check "orders, windows, generators, built-in suite" 'timeout $TMO "$OUT/unit_tests" > "$OUT/unit.out" 2>&1 || { cat "$OUT/unit.out"; false; }'

echo "== 2. clean runs: must report no errors"
n=0
for f in "Address-in-address" "Solid and checker" "Stressapptest-style patterns" "Classic marches" "Bus turnaround" "Write/read adjacency" "Normal cached traffic" \
         "Cross-core verify" "Pointer chase" "Bulk copy" "Read/write storm, hash data" "Read/write storm, random on both" "Evolving data" "Cache coherency" "Page shuffle"; do
  n=$((n + 1)); log="$OUT/clean$n.log"; rm -f "$log"
  # TSan exclusion, on purpose and not a suppression
  if [ "$SAN" = thread ] && [ "${f#Read/write storm}" != "$f" ]; then echo "  SKIP  clean: $f (races by design, see the comment above)"; continue; fi
  # every run uses a fresh random seed, so a failure here must say which one to repeat with --seed
  check "clean: $f" 'timeout $TMO "$OUT/cli" $RUN -f "$f" --log "$log" > "$OUT/clean$n.out" 2>&1; rc=$?; [ $rc -eq 0 ] && ! grep -q "^ERROR" "$log" 2>/dev/null || { grep "run seed" "$OUT/clean$n.out"; tail -5 "$OUT/clean$n.out"; false; }'
done

echo "== 2b. option checking and coverage counter"
for bad in "-mb abc" "-t abc" "-l x" "--minutes abc" "--coverage -5" "--cache bogus" "--flush bogus" "--rng bogus" "-mb" "--seed abc" "--seed -1" "--seed +5" "--seed 0x" "--seed 0xZZ" "--seed 18446744073709551616" "--seed"; do
  check "bad option rejected with exit 2: $bad" 'timeout $TMO "$OUT/cli" $bad --list > /dev/null 2>&1; [ $? -eq 2 ]'
done
check "valid option values still accepted" 'timeout $TMO "$OUT/cli" -mb 64 -t 2 --cache bypass --flush clflush --rng murmur --minutes 0.5 --coverage 100 --list > /dev/null 2>&1'
check "--version prints the version and exits 0" 'v=$(timeout $TMO "$OUT/cli" --version) && [ "${v#ddr5test_cli }" != "$v" ] && [ "${#v}" -gt 14 ]'
check "the version is on the console and in the log header of a run (same text as --version)" 'v=$(timeout $TMO "$OUT/cli" --version); v=${v#ddr5test_cli }; timeout $TMO "$OUT/cli" $RUN -f "Bulk copy" --log "$OUT/ver.log" > "$OUT/ver.out" 2>&1; head -3 "$OUT/ver.out" | grep -qxF "ddr5test $v" && grep -qF ", version $v ----" "$OUT/ver.log" || { head -3 "$OUT/ver.out"; head -2 "$OUT/ver.log"; false; }'
check "seed accepted as decimal, hex, 0 and the 64-bit maximum" 'for sv in 12345 0xDEADBEEF 0 18446744073709551615 0xFFFFFFFFFFFFFFFF; do timeout $TMO "$OUT/cli" --seed $sv --list > /dev/null 2>&1 || exit 1; done'
check "xorshift RNG option runs clean (hash tests with the new final mix)" 'timeout $TMO "$OUT/cli" $RUN --rng xorshift -f "Random-order hash soak" --log "$OUT/xs.log" > "$OUT/xs.out" 2>&1; rc=$?; [ $rc -eq 0 ] && ! grep -q "^ERROR" "$OUT/xs.log" 2>/dev/null || { grep "run seed" "$OUT/xs.out"; false; }'
check "--fpu: FPU stress threads next to a memory test cause no false error, and the run ends (exit 0, threads joined)" 'timeout $TMO "$OUT/cli" $RUN --fpu -f "Solid and checker" --log "$OUT/fpu.log" > "$OUT/fpu.out" 2>&1; rc=$?; [ $rc -eq 0 ] && ! grep -q "^ERROR" "$OUT/fpu.log" 2>/dev/null || { echo "    rc=$rc"; grep "run seed" "$OUT/fpu.out"; tail -4 "$OUT/fpu.out"; false; }'
log="$OUT/cov.log"; rm -f "$log"
timeout $TMO "$OUT/cli" -mb 2 -t 1 --no-large-pages -f "Bulk copy" --log "$log" > "$OUT/cov.out" 2>&1
check "verified-passes counter exact on a 2 MB region (3 full verifies = 3.0x)" 'grep -q "verified passes this test 3.0x" "$OUT/cov.out" || { grep passes "$OUT/cov.out"; false; }'
check "coverage on that 2 MB region is 100.0% of its 512 pages" 'grep -q "total coverage: 100.0% of the 512 pages" "$OUT/cov.out" || { grep coverage "$OUT/cov.out"; false; }'
# --coverage N now stops on real pages covered (coverage.h), not on bytes verified.
covpct() { sed -n 's/^total coverage: \([0-9.]*\)%.*/\1/p' "$1" | head -1; }
timeout $TMO "$OUT/cli" -mb 256 -t 2 --no-large-pages -f "Idle gaps" --coverage 100 --log "$OUT/cov100.log" > "$OUT/cov100.out" 2>&1; cov100_rc=$?
check "--coverage 100 stops only when every page was verified (map at 100.0%, reason given, exit 0)" '[ $cov100_rc -eq 0 ] && [ "$(covpct "$OUT/cov100.out")" = "100.0" ] && grep -q "coverage target reached" "$OUT/cov100.out" || { echo "    rc=$cov100_rc"; grep "coverage\|passes" "$OUT/cov100.out"; false; }'
timeout $TMO "$OUT/cli" -mb 256 -t 2 --no-large-pages -f "Idle gaps" --coverage 50 --log "$OUT/cov50.log" > "$OUT/cov50.out" 2>&1; cov50_rc=$?
check "--coverage 50 stops at about half: not before 50% of the pages, not after the whole region (exit 0)" 'p=$(covpct "$OUT/cov50.out"); [ $cov50_rc -eq 0 ] && [ -n "$p" ] && awk -v p="$p" "BEGIN{exit !(p >= 50 && p < 100)}" && grep -q "coverage target reached" "$OUT/cov50.out" || { echo "    rc=$cov50_rc"; grep "coverage\|passes" "$OUT/cov50.out"; false; }'

echo "== 2c. run seed: printed, logged, accepted back, and it reproduces the run"
rs() { grep -o "run seed: 0x[0-9A-F]*" "$1" | head -1 | sed 's/run seed: //'; }   # the seed a run printed
S1="$OUT/seed1"; S2="$OUT/seed2"; rm -f "$S1".* "$S2".*
timeout $TMO "$OUT/cli" -mb 2 -t 1 --no-large-pages -f "Bulk copy" --log "$S1.log" > "$S1.out" 2>&1
timeout $TMO "$OUT/cli" -mb 2 -t 1 --no-large-pages -f "Bulk copy" --log "$S2.log" > "$S2.out" 2>&1
check "each run prints a seed, and two runs get different ones" '[ -n "$(rs "$S1.out")" ] && [ -n "$(rs "$S2.out")" ] && [ "$(rs "$S1.out")" != "$(rs "$S2.out")" ]'
check "the seed is also in the log file header" 'grep -q "seed $(rs "$S1.out")" "$S1.log"'
check "random seeds are labelled as random" 'grep -q "run seed: 0x[0-9A-F]* (random)" "$S1.out"'
timeout $TMO "$OUT/cli" -mb 2 -t 1 --no-large-pages -f "Bulk copy" --seed 777 --log "$S2.log" > "$S2.out" 2>&1
check "--seed 777 is echoed as 0x0000000000000309 (given)" 'grep -q "run seed: 0x0000000000000309 (given)" "$S2.out"'
SEEDP="$(rs "$S1.out")"; timeout $TMO "$OUT/cli" -mb 2 -t 1 --no-large-pages -f "Bulk copy" --seed "$SEEDP" --log "$S2.log" > "$S2.out" 2>&1
check "the printed seed is accepted back unchanged" '[ "$(rs "$S2.out")" = "$SEEDP" ] && grep -q "(given)" "$S2.out"'
# End to end: the injected faults of the evolving-data test print hash data derived from the seed
for tag in A B C; do sv=777; [ $tag = C ] && sv=778
  timeout $TMO "$OUT/cli_inject" $RUN -f "Evolving data" --seed $sv --log "$OUT/rep$tag.log" > "$OUT/rep$tag.out" 2>&1
  grep "qword" "$OUT/rep$tag.log" | sort > "$OUT/rep$tag.data"
  grep "^ERROR" "$OUT/rep$tag.log" | grep -o " off=0x[0-9A-F]* " | sort > "$OUT/rep$tag.off"
done
check "same seed -> identical data and fault lines (run reproduced)" '[ -s "$OUT/repA.data" ] && cmp -s "$OUT/repA.data" "$OUT/repB.data"'
check "different seed -> different data" '! cmp -s "$OUT/repA.data" "$OUT/repC.data"'
check "every ERROR line carries the region offset, one per error" '[ -s "$OUT/repA.off" ] && [ "$(wc -l < "$OUT/repA.off")" = "$(grep -c "^ERROR" "$OUT/repA.log")" ]'
check "region offsets are line-aligned and inside the 64 MiB test region" '(while read -r o; do v=$((${o#off=})); [ $((v % 64)) -eq 0 ] && [ $v -lt $((64 * 1024 * 1024)) ] || exit 1; done < "$OUT/repA.off")'
check "same seed -> identical region offsets (virtual addresses differ, offsets do not)" '[ -s "$OUT/repA.off" ] && cmp -s "$OUT/repA.off" "$OUT/repB.off"'

echo "== 2d. log file that cannot be opened"
timeout $TMO "$OUT/cli" $RUN -f "Bulk copy" --log /nonexistent/dir/x.log > "$OUT/logbad.out" 2>&1; rc=$?
check "explicit --log that cannot be opened: exit 2 with the path and reason, before any test runs" '[ $rc -eq 2 ] && grep -q "cannot open log file '"'"'/nonexistent/dir/x.log'"'"'" "$OUT/logbad.out" && ! grep -q "\[loop" "$OUT/logbad.out" || { cat "$OUT/logbad.out"; false; }'
mkdir -p "$OUT/rodir"; ( cd "$OUT/rodir" && mkdir ddr5test.log && timeout $TMO "$OUT/cli" $RUN -f "Bulk copy" > "$OUT/logdef.out" 2>&1; echo $? > "$OUT/logdef.rc" )   # a directory named like the default log: fopen fails even as root
check "default log name that cannot be opened: warns and still exits 0 on a clean run" '[ "$(cat "$OUT/logdef.rc")" = "0" ] && grep -q "cannot open log file '"'"'ddr5test.log'"'"'" "$OUT/logdef.out" && grep -q "continuing without it" "$OUT/logdef.out" || { cat "$OUT/logdef.out"; false; }'

echo "== 2e. CPU restriction: thread count and pinning follow taskset, not the machine's CPU count"
# the CPUs this script may use, one number per line
allowed=$(taskset -pc $$ 2>/dev/null | sed 's/.*: //' | awk -F, '{for (i = 1; i <= NF; i++) { n = split($i, r, "-"); if (n == 2) for (j = r[1]; j <= r[2]; j++) print j; else print $i } }')
# start the tester on CPUs $1 and $2 with two workers
workers_pinned() {
  taskset -c "$1,$2" "$OUT/cli" -mb 64 -t 2 --no-large-pages --test-seconds 8 -f "Address-in-address" --log "$OUT/pin.log" > "$OUT/pin.out" 2>&1 &
  local p=$! i m rc=1 t
  for i in $(seq 1 40); do
    kill -0 $p 2>/dev/null || break
    m=$(for t in /proc/$p/task/*; do sed -n 's/^Cpus_allowed_list:[[:space:]]*//p' "$t/status" 2>/dev/null; done)
    if echo "$m" | grep -qx "$1" && echo "$m" | grep -qx "$2"; then rc=0; break; fi
    sleep 0.2
  done
  kill $p 2>/dev/null; wait $p 2>/dev/null
  return $rc
}
if [ "$(echo "$allowed" | grep -c .)" -ge 2 ]; then
  two=$(echo "$allowed" | head -2 | paste -sd, -); one=$(echo "$allowed" | head -1)
  # no -t: the default thread count is what the process may use, so it must follow the restriction
  check "taskset -c $two: 2 threads by default, clean run, every thread pinned" 'taskset -c $two timeout $TMO "$OUT/cli" -mb 64 --no-large-pages --test-seconds 3 -f "Address-in-address" --log "$OUT/ts2.log" > "$OUT/ts2.out" 2>&1; rc=$?; [ $rc -eq 0 ] && grep -q "^2 threads," "$OUT/ts2.out" && ! grep -q "could not be pinned" "$OUT/ts2.out" && ! grep -q "^ERROR" "$OUT/ts2.log" 2>/dev/null || { echo "    rc=$rc"; head -3 "$OUT/ts2.out"; false; }'
  check "taskset -c $one: 1 thread by default, clean run" 'taskset -c $one timeout $TMO "$OUT/cli" -mb 64 --no-large-pages --test-seconds 3 -f "Address-in-address" --log "$OUT/ts1.log" > "$OUT/ts1.out" 2>&1; rc=$?; [ $rc -eq 0 ] && grep -q "^1 threads," "$OUT/ts1.out" && ! grep -q "^ERROR" "$OUT/ts1.log" 2>/dev/null || { echo "    rc=$rc"; head -3 "$OUT/ts1.out"; false; }'
  # the two highest allowed CPUs
  top1=$(echo "$allowed" | tail -2 | head -1); top2=$(echo "$allowed" | tail -1)
  check "workers are pinned one per allowed CPU ($top1 and $top2)" 'workers_pinned $top1 $top2 || { echo "    thread CPU lists never showed one worker each on $top1 and $top2"; head -3 "$OUT/pin.out"; false; }'
else
  echo "  SKIP  taskset not available, or fewer than 2 usable CPUs"
fi

echo "== 2f. allocation refused by the OS: retried at smaller sizes, or a clear failure"
# An address-space limit (ulimit -v) makes mmap refuse a big region
avail_kb=$(awk '/^MemAvailable:/ {print $2}' /proc/meminfo 2>/dev/null); avail_kb=${avail_kb:-0}
if [ -n "$SAN" ]; then
  echo "  SKIP  allocation retry (sanitizer builds cannot run under an address-space limit)"
elif [ "$avail_kb" -lt 3145728 ]; then
  echo "  SKIP  allocation retry (needs 3 GiB of available memory, have $((avail_kb / 1024)) MiB)"
elif ! ( ulimit -v 262144 && "$OUT/cli" --list > /dev/null 2>&1 ); then
  echo "  SKIP  allocation retry (the tester cannot start under a 256 MiB address-space limit here)"
else
  log="$OUT/c3a.log"; rm -f "$log"
  ( ulimit -v 786432; timeout $TMO "$OUT/cli" -mb 1024 -t 2 --no-large-pages --test-seconds 2 -f "Solid and checker" --log "$log" > "$OUT/c3a.out" 2>&1 ); rc=$?
  check "1 GiB refused under a 768 MiB limit: retried with a log line each, runs on the smaller size, exit 0, no error" '[ $rc -eq 0 ] && grep -q "^allocation of 1024 MB failed; trying " "$OUT/c3a.out" && grep -q "^testing [0-9.]* GiB instead of the 1.00 GiB planned (" "$OUT/c3a.out" && grep -q "^finished in" "$OUT/c3a.out" && ! grep -q "^ERROR" "$log" 2>/dev/null || { echo "    rc=$rc"; head -12 "$OUT/c3a.out"; false; }'
  check "the log header and the summary name the size that was really allocated, not the planned one" '! grep -q "ddr5test run, 1.00 GiB" "$log" && grep -q "ddr5test run, 0\.[0-9]* GiB" "$log" && grep -q "of the 0\.[0-9]* GiB tested region" "$OUT/c3a.out" || { cat "$log" | head -3; tail -3 "$OUT/c3a.out"; false; }'
  log="$OUT/c3b.log"; rm -f "$log"
  ( ulimit -v 262144; timeout $TMO "$OUT/cli" -mb 2048 -t 2 --no-large-pages --test-seconds 2 -f "Solid and checker" --log "$log" > "$OUT/c3b.out" 2>&1 ); rc=$?
  check "refused at every size (down to 20%): says so, exit 2, no test started" '[ $rc -eq 2 ] && grep -q "^allocation failed at every size from 2048 MB down to [0-9]* MB (9 attempts)" "$OUT/c3b.out" && ! grep -q "\[loop" "$OUT/c3b.out" || { echo "    rc=$rc"; tail -4 "$OUT/c3b.out"; false; }'
fi

echo "== 3. fault injection: planted faults must be found"
. tools/inject_checks.sh   # the portable checks (fill, page shuffle, coherency, evolve, march, chase, copy, rwstorm, stop-on-error); the same file runs on the Windows runner
inject_checks_portable "$OUT/cli_inject" "$OUT"
check "fill: no log-write warning when the log is healthy" '! grep -q "cannot write to the log file" "$OUT/inj_fill.out"'

echo "== 3b. coverage audit (E2f): the page map must never claim more than the engine really compared"
# The fault-injection build records every 64-byte line that IO::verify / the chase compares (src/inject.cpp,
# independent of CoverageMap) and prints one "audit:" line after the coverage line. marked_not_compared must always
# be 0 (map = subset of the audit); compared_not_marked may be above 0 only where a stop cut a job short.
audit_f() { sed -n 's/^audit:.*[[:space:]]'"$2"'=\(-\{0,1\}[0-9]\{1,\}\).*/\1/p' "$1" | head -1; }
# audit_ok FILE: the audit line exists and the map is a subset of it
audit_ok() { [ -n "$(audit_f "$1" pages)" ] && [ "$(audit_f "$1" marked_not_compared)" = "0" ]; }
# audit_show FILE: the audit line, for a failure message
audit_show() { grep -E "^total coverage|^audit" "$1" | cut -c1-200; }
n=0
for f in "Solid and checker" "Cross-core verify" "Classic marches" "Evolving data" "Read/write storm, hash data" "Bulk copy" "Page shuffle" "Pointer chase" "Idle gaps"; do
  n=$((n + 1)); aout="$OUT/aud_full$n.out"
  if [ "$SAN" = thread ] && [ "${f#Read/write storm}" != "$f" ]; then echo "  SKIP  audit, full run: $f (races by design, see the comment above)"; continue; fi
  timeout $TMO "$OUT/cli_inject" $RUN -f "$f" --log "$OUT/aud_full$n.log" > "$aout" 2>&1
  check "audit, full run: $f: map = audit = every page of the region (100%)" 'audit_ok "$aout" && p=$(audit_f "$aout" pages) && [ "$p" -gt 0 ] && [ "$(audit_f "$aout" marked)" = "$p" ] && [ "$(audit_f "$aout" compared)" = "$p" ] && [ "$(audit_f "$aout" compared_not_marked)" = "0" ] || { audit_show "$aout"; false; }'
done
timeout $TMO "$OUT/cli_inject" $RUN -f "Cache coherency" --log "$OUT/aud_coh.log" > "$OUT/aud_coh.out" 2>&1
check "audit, full run: Cache coherency verifies no region lines and the map marks none (by design)" 'audit_ok "$OUT/aud_coh.out" && [ "$(audit_f "$OUT/aud_coh.out" marked)" = "0" ] && [ "$(audit_f "$OUT/aud_coh.out" compared)" = "0" ] || { audit_show "$OUT/aud_coh.out"; false; }'
# --coverage 100 and the two controls use "Idle gaps" on 64 MB / 2 threads: every thread's window is its whole chunk
CV="-mb 64 -t 2 --no-large-pages --test-seconds 3 -f Idle\ gaps --coverage 100"
eval "timeout $TMO \"$OUT/cli_inject\" $CV --log \"$OUT/aud_c100.log\" > \"$OUT/aud_c100.out\" 2>&1"
check "audit, --coverage 100: stops on its own with the reason, and the audit saw every page too" 'audit_ok "$OUT/aud_c100.out" && p=$(audit_f "$OUT/aud_c100.out" pages) && grep -q "coverage target reached" "$OUT/aud_c100.out" && [ "$(audit_f "$OUT/aud_c100.out" marked)" = "$p" ] && [ "$(audit_f "$OUT/aud_c100.out" compared)" = "$p" ] || { audit_show "$OUT/aud_c100.out"; false; }'
# negative control 1: thread 0 skips the verify of its whole window (and, like a real skipping algo, does not mark it)
eval "DDR5TEST_SKIP_WINDOW=1 timeout $TMO \"$OUT/cli_inject\" $CV --log \"$OUT/aud_skip.log\" > \"$OUT/aud_skip.out\" 2>&1"
check "audit, control: a skipped window leaves exactly its pages unmarked (first half of the region) and the audit agrees" 'audit_ok "$OUT/aud_skip.out" && p=$(audit_f "$OUT/aud_skip.out" pages) && h=$((p / 2)) && [ "$(audit_f "$OUT/aud_skip.out" marked)" = "$h" ] && [ "$(audit_f "$OUT/aud_skip.out" compared)" = "$h" ] && [ "$(audit_f "$OUT/aud_skip.out" compared_not_marked)" = "0" ] && [ "$(audit_f "$OUT/aud_skip.out" unmarked_first)" = "0" ] && [ "$(audit_f "$OUT/aud_skip.out" unmarked_last)" = "$((h - 1))" ] || { audit_show "$OUT/aud_skip.out"; false; }'
check "audit, control: with that window skipped, --coverage 100 does NOT stop on its own (no 'coverage target reached', 50.0%)" '! grep -q "coverage target reached" "$OUT/aud_skip.out" && grep -q "^total coverage: 50.0% of" "$OUT/aud_skip.out" || { audit_show "$OUT/aud_skip.out"; false; }'
# negative control 2: the algo skips the verify but still marks the window: the audit must catch the overstated map
eval "DDR5TEST_SKIP_WINDOW=2 timeout $TMO \"$OUT/cli_inject\" $CV --log \"$OUT/aud_lie.log\" > \"$OUT/aud_lie.out\" 2>&1"
check "audit, control: marking a window that was never compared is caught (map says 100%, audit sees half; first bad page 0)" 'p=$(audit_f "$OUT/aud_lie.out" pages) && [ -n "$p" ] && [ "$(audit_f "$OUT/aud_lie.out" marked)" = "$p" ] && [ "$(audit_f "$OUT/aud_lie.out" marked_not_compared)" = "$((p / 2))" ] && [ "$(audit_f "$OUT/aud_lie.out" first_marked_not_compared)" = "0" ] || { audit_show "$OUT/aud_lie.out"; false; }'
# a stop raised in the middle of the verify (hook: after 300000 compared lines): never a superset, whatever the algo
for f in "Idle gaps" "Classic marches" "Evolving data" "Bulk copy" "Page shuffle" "Pointer chase" "Read/write storm, hash data"; do
  if [ "$SAN" = thread ] && [ "${f#Read/write storm}" != "$f" ]; then echo "  SKIP  audit, stop mid-verify: $f (races by design, see the comment above)"; continue; fi
  DDR5TEST_STOP_AFTER_COMPARES=300000 timeout $TMO "$OUT/cli_inject" $RUN -f "$f" --log "$OUT/aud_stp.log" > "$OUT/aud_stp.out" 2>&1
  check "audit, stop in the middle of a verify: $f: map is a subset of the audit and short of the whole region" 'audit_ok "$OUT/aud_stp.out" && [ "$(audit_f "$OUT/aud_stp.out" marked)" -lt "$(audit_f "$OUT/aud_stp.out" pages)" ] || { audit_show "$OUT/aud_stp.out"; false; }'
done
DDR5TEST_STOP_AFTER_COMPARES=300000 timeout $TMO "$OUT/cli_inject" $RUN -f "Idle gaps" --log "$OUT/aud_stp.log" > "$OUT/aud_stp.out" 2>&1
check "audit, stop in the middle of a verify: lines compared by the cut-short job are NOT marked (compared > marked)" 'audit_ok "$OUT/aud_stp.out" && [ "$(audit_f "$OUT/aud_stp.out" compared_not_marked)" -gt 0 ] || { audit_show "$OUT/aud_stp.out"; false; }'
# a real SIGINT at a few moments of one run (the coverage differs from machine to machine, the subset rule does not)
if [ "$SAN" = thread ]; then
  echo "  SKIP  audit, SIGINT (TSan is too slow for the timing)"
else
  audit_sigint_ok() {
    local d p i
    for d in 0.2 0.5 0.8 1.4; do
      "$OUT/cli_inject" -mb 256 -t 2 --no-large-pages --test-seconds 20 -f "Idle gaps" --log "$OUT/aud_int.log" > "$OUT/aud_int.out" 2>&1 < /dev/null &
      p=$!; sleep "$d"; kill -INT $p 2>/dev/null
      i=0
      while kill -0 $p 2>/dev/null; do
        i=$((i + 1)); if [ $i -gt 300 ]; then kill -9 $p 2>/dev/null; wait $p 2>/dev/null; echo "    SIGINT after ${d}s: still running 30 s later (killed)"; return 1; fi
        sleep 0.1
      done
      wait $p
      audit_ok "$OUT/aud_int.out" || { echo "    SIGINT after ${d}s:"; audit_show "$OUT/aud_int.out"; return 1; }
    done
  }
  check "audit, real SIGINT at 0.2 / 0.5 / 0.8 / 1.4 s: the map is never a superset of what was compared" 'audit_sigint_ok'
fi
if [ -w /dev/full ]; then
  # /dev/full opens fine and fails every flush with ENOSPC
  timeout $TMO "$OUT/cli_inject" $RUN -f "Solid and checker" --log /dev/full > "$OUT/inj_full.out" 2>&1; rc=$?
  check "log on /dev/full: exit 1 and the same 42 lines / 44 bits counted" '[ $rc -eq 1 ] && grep -q "42 bad lines, 44 bits" "$OUT/inj_full.out" || { echo "    rc=$rc"; tail -5 "$OUT/inj_full.out"; false; }'
  check "log on /dev/full: exactly one console warning, repeated in the final summary" '[ "$(grep -c "cannot write to the log file" "$OUT/inj_full.out")" = "1" ] && grep -q "writing to the log file .*/dev/full.* failed" "$OUT/inj_full.out"'
  timeout $TMO "$OUT/cli" $RUN -f "Bulk copy" --log /dev/full > "$OUT/clean_full.out" 2>&1; rc=$?
  check "log on /dev/full, clean run: warns once but still exits 0 (nothing was lost)" '[ $rc -eq 0 ] && [ "$(grep -c "cannot write to the log file" "$OUT/clean_full.out")" = "1" ] || { echo "    rc=$rc"; tail -5 "$OUT/clean_full.out"; false; }'
else
  echo "  SKIP  /dev/full not writable: log write failure checks"
fi
# CLFLUSHOPT (D1): the flush instruction is chosen at run time and named in the banner
if grep -qw clflushopt /proc/cpuinfo 2>/dev/null; then HAVE_OPT=1; else HAVE_OPT=0; fi
for fm in auto clflush clflushopt; do
  log="$OUT/inj_fl_$fm.log"; rm -f "$log"
  timeout $TMO "$OUT/cli_inject" $RUN --flush $fm -f "Solid and checker" --log "$log" > "$OUT/inj_fl_$fm.out" 2>&1; rc=$?
  check "flush $fm: exit 1, the same 42 planted lines / 44 bits found" '[ $rc -eq 1 ] && [ "$(grep -c "^ERROR" "$log")" = "42" ] && grep -q "42 bad lines, 44 bits" "$OUT/inj_fl_$fm.out" || { echo "    rc=$rc"; tail -4 "$OUT/inj_fl_$fm.out"; false; }'
done
check "flush: banner names CLFLUSH when forced" 'grep -q "^flush instruction: CLFLUSH$" "$OUT/inj_fl_clflush.out"'
if [ $HAVE_OPT = 1 ]; then
  check "flush: banner names CLFLUSHOPT for auto and clflushopt (this CPU has it)" 'grep -q "^flush instruction: CLFLUSHOPT$" "$OUT/inj_fl_auto.out" && grep -q "^flush instruction: CLFLUSHOPT$" "$OUT/inj_fl_clflushopt.out"'
else
  check "flush: without CLFLUSHOPT, auto uses CLFLUSH and clflushopt says it is not supported" 'grep -q "^flush instruction: CLFLUSH$" "$OUT/inj_fl_auto.out" && grep -q "CLFLUSHOPT not supported" "$OUT/inj_fl_clflushopt.out"'
fi
log="$OUT/inj_pagestop.log"; rm -f "$log"
DDR5TEST_STOP_IN_FILL=1 timeout $TMO "$OUT/cli_inject" $RUN -f "Page shuffle" --log "$log" > "$OUT/inj_pagestop.out" 2>&1; rc=$?
check "page shuffle: stopped mid-fill (hard stop): the final verify is skipped, so not even the planted flip is reported, and no false error from pages never filled" '[ $rc -eq 0 ] && [ "$(grep -c "^ERROR" "$log")" = "0" ] || { echo "    rc=$rc ERROR lines: $(grep -c "^ERROR" "$log")"; false; }'
# hard stop in the final verify: a flip planted in the LAST page is found by a verify that runs
log="$OUT/inj_late.log"; rm -f "$log"
DDR5TEST_FINAL_LATE_FLIP=1 timeout $TMO "$OUT/cli_inject" $RUN -f "Page shuffle" --log "$log" > "$OUT/inj_late.out" 2>&1; rc=$?
check "page shuffle: flip in the last page is found when the final verify runs to the end (control)" '[ $rc -eq 1 ] && grep "^ERROR" "$log" | grep -q "pagemix-final.* off=0x3FFF[0-9A-F]* " || { echo "    rc=$rc"; grep "^ERROR" "$log" | cut -c1-140; false; }'
log="$OUT/inj_latestop.log"; rm -f "$log"
DDR5TEST_FINAL_LATE_FLIP=1 DDR5TEST_STOP_IN_FINAL=1 timeout $TMO "$OUT/cli_inject" $RUN -f "Page shuffle" --log "$log" > "$OUT/inj_latestop.out" 2>&1; rc=$?
check "page shuffle: stop raised during the final verify ends it: the late flip is not reported, nothing false is" '[ $rc -le 1 ] && ! grep "^ERROR" "$log" | grep -q " off=0x3FFF[0-9A-F]* " && [ "$(grep "^ERROR" "$log" | grep -vc "pagemix-final.* off=0x3[0-9A-F]\{3\} ")" = "0" ] || { echo "    rc=$rc"; grep "^ERROR" "$log" | cut -c1-140; false; }'
log="$OUT/inj_evostop.log"; rm -f "$log"
DDR5TEST_STOP_IN_EVOLVE=1 timeout $TMO "$OUT/cli_inject" $RUN -f "Evolving data" --log "$log" > "$OUT/inj_evostop.out" 2>&1; rc=$?
check "evolve: stopped mid-rewrite reports no errors (lines the cut-short pass never reached are not verified)" '[ $rc -eq 0 ] && [ "$(grep -c "^ERROR" "$log" 2>/dev/null)" = "0" ] || { echo "    rc=$rc ERROR lines: $(grep -c "^ERROR" "$log" 2>/dev/null)"; grep "run seed" "$OUT/inj_evostop.out"; false; }'
log="$OUT/inj_copystop.log"; rm -f "$log"
DDR5TEST_STOP_IN_COPY_FILL=1 timeout $TMO "$OUT/cli_inject" $RUN -f "Bulk copy" --log "$log" > "$OUT/inj_copystop.out" 2>&1; rc=$?
check "copy: stopped mid-fill ends the fill, skips copy and verify: exit 0, nothing reported (neither the planted faults nor never-written lines)" '[ $rc -eq 0 ] && [ "$(grep -c "^ERROR" "$log")" = "0" ] && grep -q " STOP " "$OUT/inj_copystop.out" || { echo "    rc=$rc ERROR lines: $(grep -c "^ERROR" "$log")"; tail -4 "$OUT/inj_copystop.out"; false; }'
log="$OUT/inj_throw.log"; rm -f "$log"
DDR5TEST_THROW_IN_WORKER=1 timeout $TMO "$OUT/cli_inject" $RUN -f "Solid and checker" --fpu --log "$log" > "$OUT/inj_throw.out" 2>&1; rc=$?
check "worker exception: exit 3 and 'internal error' logged (no terminate, no hang; --fpu threads joined)" '[ $rc -eq 3 ] && grep -q "internal error: injected worker failure" "$OUT/inj_throw.out" || { echo "    rc=$rc"; tail -5 "$OUT/inj_throw.out"; false; }'
# Barrier protocol (G1): worker 0 makes one sync() call more than worker 1 in a run that is never
log="$OUT/inj_sync.log"; rm -f "$log"
DDR5TEST_EXTRA_SYNC=1 timeout $TMO "$OUT/cli_inject" $RUN -f "Solid and checker" --log "$log" > "$OUT/inj_sync.out" 2>&1; rc=$?
check "barrier protocol: an extra sync() on one worker is caught (abort with the worker numbers, no hang, no pass)" '[ $rc -ge 128 ] && grep -q "barrier protocol violated in .*: worker 1 made [0-9]* sync() calls, worker 0 made [0-9]*" "$OUT/inj_sync.out" || { echo "    rc=$rc"; tail -4 "$OUT/inj_sync.out"; false; }'

echo "== 4. stop: SIGINT at several moments in every test; the run must end within STOPWAIT seconds, exit 0, report no error"
# The first Ctrl-C is the stop request (the CLI ends the current step and prints the summary).
STOPWAIT=${STOPWAIT:-5}
case "$SAN" in *address*) STOPWAIT=$((STOPWAIT * 3)) ;; esac
STOPAT="0.1 0.5 1.2 2.5"
# stop_test_ok NAME
stop_test_ok() {
  local d p i hit
  for d in $STOPAT; do
    rm -f "$OUT/stp.log"
    "$OUT/cli" -mb 128 -t 2 --no-large-pages --test-seconds 20 -f "$1" --log "$OUT/stp.log" > "$OUT/stp.out" 2>&1 < /dev/null &
    p=$!; sleep "$d"
    hit=1; kill -INT $p 2>/dev/null || hit=0
    i=0
    while kill -0 $p 2>/dev/null; do
      i=$((i + 1))
      if [ $i -gt $((STOPWAIT * 10)) ]; then kill -9 $p 2>/dev/null; wait $p 2>/dev/null; echo "    SIGINT after ${d}s: still running ${STOPWAIT}s later (killed); last line: $(tail -1 "$OUT/stp.out")"; return 1; fi
      sleep 0.1
    done
    wait $p; rc=$?
    if [ $rc -ne 0 ] || grep -q "^ERROR" "$OUT/stp.log" 2>/dev/null; then
      echo "    SIGINT after ${d}s (running: $hit): rc=$rc ERROR lines: $(grep -c "^ERROR" "$OUT/stp.log" 2>/dev/null)"; grep "run seed" "$OUT/stp.out"; tail -3 "$OUT/stp.out"; return 1
    fi
  done
}
if [ "$SAN" = thread ]; then
  echo "  SKIP  stop checks under TSan (the read/write storm tests race by design and TSan is too slow for the timing)"
else
  mapfile -t STOPNAMES < <("$OUT/cli" --list | sed -E 's/^ *[0-9]+ +(Q +)?[a-z0-9]+ +//')
  check "the built-in suite lists 28 tests to stop-test" '[ ${#STOPNAMES[@]} -eq 28 ]'
  for TNAME in "${STOPNAMES[@]}"; do
    check "stop: $TNAME" 'stop_test_ok "$TNAME"'
  done
fi

echo
if [ $fail -eq 0 ]; then echo "SELFTEST PASSED"; else echo "SELFTEST FAILED (outputs in $OUT)"; fi
exit $fail
