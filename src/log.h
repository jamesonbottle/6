#pragma once
// log.h - output sink and progress-line helpers.
#include <functional>
#include <string>

namespace ddr5 {

constexpr int kStatusWidth = 118;

// printf-style formatting appended to `s`.
void appendf(std::string& s, const char* fmt, ...);

// Where text output goes.
struct Logger {
    std::function<void(const std::string&)> sink;
    bool console = false; // console front-end: live progress output
    bool tty = true;
    void emit(const std::string& t) const;
    void emitf(const char* fmt, ...) const;
    void print_clear() const;
};

} // namespace ddr5
