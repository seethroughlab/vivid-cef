#pragma once

#include <functional>

class BrowserCefGate {
public:
    using AcquireFn = std::function<bool()>;

    void set_acquire_fn(AcquireFn fn);
    bool ensure_acquired();
    bool is_acquired() const { return acquired_; }
    bool consume_should_log_failure();

private:
    bool acquired_ = false;
    bool should_log_failure_ = false;
    AcquireFn acquire_fn_;
};

