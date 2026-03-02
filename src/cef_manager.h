#pragma once

// CefManager — reference-counted CEF lifecycle singleton.
//
// BrowserOp instances call acquire()/release() to manage CEF's process-wide
// lifetime. CEF is initialized on first acquire and shut down on last release.
// pump_once() drives the CEF message loop, deduped per frame.

#include <atomic>
#include <cstdint>
#include <string>

class CefManager {
public:
    // Increment refcount; initialize CEF on first call.
    // Returns true if CEF is (now) initialized.
    static bool acquire();

    // Decrement refcount; shut down CEF when it reaches zero.
    static void release();

    // Pump the CEF message loop once per frame (skips if already pumped this frame).
    static void pump_once(uint64_t frame);

    // True if CEF is currently initialized.
    static bool is_initialized();

    // Path to the vivid-cef-helper executable (discovered from plugin dylib location).
    static std::string helper_path();

private:
    static std::atomic<int>      s_refcount;
    static std::atomic<bool>     s_initialized;
    static std::atomic<uint64_t> s_last_pumped_frame;
};
