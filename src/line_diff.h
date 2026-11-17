#pragma once
// line_diff.h - the pure parts of an error report (Run::report): what differs in one 64-byte line (diff_line),
// what a re-read of it tells (classify_reread) and the text written to the console / log (format_error_block).
// No memory access, no counters, no I/O: Run::report does those and calls these.
#include <cstring>
#include <string>
#include "log.h"
#include "types.h"

namespace ddr5 {

// beat b = the b-th 32-bit word of the line in memory order (16 beats per 64-byte line)
constexpr int kBeatsPerLine = 16;

// Which bits of one line differ.
struct LineDiff {
    u32 beatXor[kBeatsPerLine] = {}; // expected ^ actual, per beat
    u32 beatMask = 0;                // bit b set = beat b differs
    u32 laneMask = 0;                // bit l set = bit l (data lane) differs in some beat
    u64 bits = 0;                    // wrong bits in the line
    int perBlock[4] = {0, 0, 0, 0};  // wrong bits per byte lane (x8 device) of each beat, summed over the beats
};

inline LineDiff diff_line(const Line& e, const Line& a) {
    static_assert(sizeof(Line) == kLineBytes, "diff_line reads a Line as 16 beats of 32 bits");
    u32 eb[kBeatsPerLine], ab[kBeatsPerLine];
    memcpy(eb, &e, kLineBytes);
    memcpy(ab, &a, kLineBytes);
    LineDiff d;
    for (int b = 0; b < kBeatsPerLine; b++) {
        const u32 x = eb[b] ^ ab[b];
        d.beatXor[b] = x;
        if (!x)
            continue;
        d.beatMask |= 1u << b;
        d.laneMask |= x;
        d.bits += (u64)pc32(x);
        for (int g = 0; g < 4; g++)
            d.perBlock[g] += pc32((x >> (8 * g)) & 0xFFu);
    }
    return d;
}

// On-die ECC (128 data bits + 8 check bits per codeword) corrects 1 bit invisibly, so the interesting question
// is how many bits of one x8 codeword are wrong: class 0 = 1 bit, 1 = 2 bits, 2 = 3-4 bits, 3 = 5 or more,
// -1 = none.
inline int codeword_class(int wrongBits) {
    if (wrongBits <= 0)
        return -1;
    return wrongBits == 1 ? 0 : wrongBits == 2 ? 1 : wrongBits <= 4 ? 2 : 3;
}

// How many of the four byte lanes fall in each class (the `x8-codewords[1b/2b/3-4b/5+b]` field).
struct CodewordCounts {
    int c[4] = {0, 0, 0, 0};
};
inline CodewordCounts codeword_counts(const LineDiff& d) {
    CodewordCounts n;
    for (int k = 0; k < 4; k++) {
        const int cl = codeword_class(d.perBlock[k]);
        if (cl >= 0)
            n.c[cl]++;
    }
    return n;
}

// What reading the line again showed.
enum class Reread {
    LogicalCheck, // not a memory line (a computed value was wrong): nothing to read again
    Skipped,      // the re-read is only done for the first errors of a test
    Transient,    // the second read gave the expected data
    Stable,       // the second read gave the same wrong data
    Unstable,     // the second read gave something else
};

inline Reread classify_reread(const Line& again, const Line& e, const Line& a) {
    if (!memcmp(&again, &e, kLineBytes))
        return Reread::Transient;
    if (!memcmp(&again, &a, kLineBytes))
        return Reread::Stable;
    return Reread::Unstable;
}

inline const char* reread_text(Reread r) {
    switch (r) {
    case Reread::LogicalCheck:
        return "(logical check, not a memory line)";
    case Reread::Skipped:
        return "n/a";
    case Reread::Transient:
        return "TRANSIENT(re-read ok: read path/signalling)";
    case Reread::Stable:
        return "STABLE(cell/write path/addressing)";
    case Reread::Unstable:
        break;
    }
    return "UNSTABLE(changes every read)";
}

// The block written for one bad line: a header line and one line per differing qword. Ends with a newline.
inline std::string format_error_block(double seconds, const std::string& test, const char* phase, int tid,
                                      const void* addr, u64 off, const LineDiff& d, Reread rr, const Line& e,
                                      const Line& a) {
    const CodewordCounts cw = codeword_counts(d);
    std::string msg;
    appendf(msg,
            "ERROR t=%.1fs test=\"%s\" %s thr=%d addr=%p off=0x%llX beats=0x%04X lanes=0x%08X bits=%llu "
            "x8-codewords[1b/2b/3-4b/5+b]=%d/%d/%d/%d %s\n",
            seconds, test.c_str(), phase, tid, addr, (unsigned long long)off, d.beatMask, d.laneMask,
            (unsigned long long)d.bits, cw.c[0], cw.c[1], cw.c[2], cw.c[3], reread_text(rr));
    for (int j = 0; j < 8; j++)
        if (e.w[j] != a.w[j])
            appendf(msg, "      qword %d: exp=%016llX act=%016llX xor=%016llX\n", j, (unsigned long long)e.w[j],
                    (unsigned long long)a.w[j], (unsigned long long)(e.w[j] ^ a.w[j]));
    return msg;
}

} // namespace ddr5
