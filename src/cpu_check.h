#pragma once
// cpu_check.h - may the AVX2 code of this program run on this machine? (AUDIT_PLAN.md E3)
namespace ddr5 {

enum class Avx2Status {
    Ok, // the CPU has AVX2 and the OS saves the YMM registers
    NoCpu, // the CPU does not report AVX2 (or AVX)
    NoOs
};

// The decision on its own, from the raw CPU data
Avx2Status avx2_decide(bool leaf7Ok, unsigned l1ecx, unsigned l7ebx, unsigned long long xcr0);

// Reads this CPU and decides.
Avx2Status avx2_status();

// Text for the user (plain ASCII, no trailing newline).
const char* avx2_message(Avx2Status s);

} // namespace ddr5
