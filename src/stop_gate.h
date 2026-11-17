// What the GUI's Stop button and its window close do while a run is going.
#pragma once

namespace ddr5 {

class StopGate {
  public:
    enum class Act {
        None, // nothing to do
        RequestStop,
        ForceExit,
        CloseNow // no run is going: destroy the window
    };

    explicit StopGate(double delaySeconds = 3.0) : delay_(delaySeconds) {}

    void on_run_start() {
        running_ = true;
        stopping_ = false;
        closing_ = false;
        offered_ = false;
        stopAt_ = 0;
    }

    // The Stop button was clicked while a run is going.
    Act on_stop_click(double now) {
        if (!running_)
            return Act::None;
        if (!stopping_)
            return begin_stop(now);
        return offered_ ? Act::ForceExit : Act::None;
    }

    // The user closed the window (X, Alt+F4, system menu).
    Act on_close(double now) {
        if (!running_)
            return Act::CloseNow;
        closing_ = true;
        if (!stopping_)
            return begin_stop(now);
        return offered_ ? Act::ForceExit : Act::None;
    }

    // Called from the UI timer.
    bool tick(double now) {
        if (!running_ || !stopping_ || offered_ || now - stopAt_ < delay_)
            return false;
        offered_ = true;
        return true;
    }

    // The worker's done message arrived.
    Act on_run_done() {
        running_ = false;
        stopping_ = false;
        offered_ = false;
        return closing_ ? Act::CloseNow : Act::None;
    }

    bool running() const { return running_; }
    bool stopping() const { return stopping_; }
    bool closing() const { return closing_; }
    bool force_offered() const { return offered_; }

  private:
    Act begin_stop(double now) {
        stopping_ = true;
        stopAt_ = now;
        return Act::RequestStop;
    }

    double delay_;
    double stopAt_ = 0;
    bool running_ = false, stopping_ = false, closing_ = false, offered_ = false;
};

} // namespace ddr5
