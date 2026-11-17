// audit.h - E2f: the line-level audit behind the coverage map (fault-injection builds only).
// IO::verify (lineio.h) and the pointer chase call audit_line() for every 64-byte line they really compare,
// independent of CoverageMap. The selftest then checks that the map never claims more than was compared.
#pragma once

#ifdef DDR5TEST_INJECT
namespace ddr5::inject {

// Records that the line at `a` was compared (ignored when `a` is outside the tested region).
void audit_line(const void* a);

} // namespace ddr5::inject
#endif
