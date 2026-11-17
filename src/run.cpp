// run.cpp - Run::report (classify, format, write) and write_error_log.
#include "run.h"
#include <cerrno>
#include <cstring>
#include "lineio.h"

namespace ddr5 {

// Reads the line again (flushed, so it comes from DRAM, not the cache) and says what that showed.
static Reread reread_line(const u8* addr, const Line& e, const Line& a) {
    _mm_clflush(addr);
    _mm_mfence();
    Line r;
    IO<32, 0, 0>::load(addr, r);
    return classify_reread(r, e, a);
}

// Classify: every counter that one bad line feeds. Always happens, whether or not the line is logged.
u64 Run::count_error(const LineDiff& d, u64 off, double tnow) {
    for (int b = 0; b < kBeatsPerLine; b++) {
        if (!d.beatXor[b])
            continue;
        st.beat[b]++;
        for (int l = 0; l < 32; l++)
            if ((d.beatXor[b] >> l) & 1)
                st.lane[l]++;
    }
    for (int k = 0; k < 4; k++) {
        const int cl = codeword_class(d.perBlock[k]);
        if (cl >= 0)
            st.blk[cl]++;
    }
    const u64 nl = st.lines.fetch_add(1) + 1;
    st.bits += d.bits;
    errors.errTotal++;
    double none = -1;
    errors.firstErrTest.compare_exchange_strong(none, tnow - progress.tTestStart);
    none = -1;
    errors.firstErrRun.compare_exchange_strong(none, tnow - progress.tStart);
    // byte offset of the line in the tested region: which address bits are set in the bad lines
    for (int b = 6; b < 48; b++)
        if ((off >> b) & 1)
            st.abit[b]++;
    return nl;
}

void Run::count_reread(Reread r) {
    if (r == Reread::Transient)
        st.trans++;
    else if (r == Reread::Stable)
        st.stable++;
    else if (r == Reread::Unstable)
        st.unstable++;
}

// Write: console first, then the log file. The caller holds errors.mu, so blocks never interleave.
void Run::publish_error(const std::string& text) {
    log.print_clear();
    log.emit(text);
    write_error_log(text);
}

void Run::report(int tid, const u8* addr, const Line& e, const Line& a, const char* phase, bool reread) {
    if (aborted.load(std::memory_order_relaxed))
        return;
    // 1. classify: what differs, what a re-read shows, and count it
    const LineDiff d = diff_line(e, a);
    const double tnow = plat::now();
    const u64 off = (u64)(addr - layout.region); // byte offset of the line in the tested region
    const u64 nl = count_error(d, off, tnow);
    Reread rr = reread ? Reread::Skipped : Reread::LogicalCheck;
    if (reread && nl < 20000) { // only the first errors of a test are read again (each costs a flush)
        rr = reread_line(addr, e, a);
        count_reread(rr);
    }
    if (cfg.stopOnErr)
        stop = true;
    // Everything above is counting and always happens.
    if (errors.logged.load(std::memory_order_relaxed) >= cfg.maxLog)
        return;
    std::lock_guard<std::mutex> l(errors.mu);
    if (errors.logged.load(std::memory_order_relaxed) >= cfg.maxLog)
        return; // another thread took the last slot while this one waited
    errors.logged.fetch_add(1, std::memory_order_relaxed);
    // 2. format and 3. write (`label` is read under the lock, set_label() takes it too)
    publish_error(format_error_block(tnow - progress.tStart, label, phase, tid, (const void*)addr, off, d, rr, e, a));
}

void Run::write_error_log(const std::string& text) {
    if (!errors.logf)
        return;
    errno = 0;
    if (fputs(text.c_str(), errors.logf) != EOF && fflush(errors.logf) == 0)
        return;
    const int e = errno;
    if (!errors.logFailed.exchange(true)) {
        log.print_clear();
        log.emitf("WARNING: cannot write to the log file (%s): error details may be missing from it; errors are still "
                  "counted and reported here\n",
                  e ? strerror(e) : "write error");
    }
}

} // namespace ddr5
