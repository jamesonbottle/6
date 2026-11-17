// "Start a run" in the GUI has two steps that can throw
#pragma once
#include <exception>
#include <string>

namespace ddr5 {

// Runs `step`.
template <class Step>
bool try_start(Step&& step, std::string& why) {
    why.clear();
    try {
        step();
        return true;
    } catch (const std::exception& e) {
        why = e.what();
    } catch (...) {}
    if (why.empty())
        why = "unknown error";
    return false;
}

} // namespace ddr5
