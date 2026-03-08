#include "browser_cef_gate.h"

#include <utility>

void BrowserCefGate::set_acquire_fn(AcquireFn fn) {
    acquire_fn_ = std::move(fn);
}

bool BrowserCefGate::ensure_acquired() {
    if (acquired_) return true;
    if (!acquire_fn_) return false;
    if (!acquire_fn_()) {
        should_log_failure_ = true;
        return false;
    }
    acquired_ = true;
    should_log_failure_ = false;
    return true;
}

bool BrowserCefGate::consume_should_log_failure() {
    bool out = should_log_failure_;
    should_log_failure_ = false;
    return out;
}
