#pragma once
// run_status.h - the text of the live status line that Run::parallel prints while a test runs.
#include <cstdio>
#include <string>
#include "types.h"

namespace ddr5 {

// "  [  12s] <test>   3.4 GB/s  cov  12.5%  errors: 0 (this test 0)". `coverage` is the text from percent_text()
// (coverage.h), "0.0" before the region exists; `errorsRun` is the run total, `errorsTest` the current test's.
// At most 219 characters; the test name is cut to 50.
inline std::string status_line(double elapsedSeconds, const std::string& test, double gbPerSecond,
                               const std::string& coverage, u64 errorsRun, u64 errorsTest) {
    char buf[220];
    snprintf(buf, sizeof buf, "  [%5.0fs] %-50.50s %5.1f GB/s  cov %5s%%  errors: %llu (this test %llu)",
             elapsedSeconds, test.c_str(), gbPerSecond, coverage.c_str(), (unsigned long long)errorsRun,
             (unsigned long long)errorsTest);
    return buf;
}

} // namespace ddr5
