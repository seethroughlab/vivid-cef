#include "cef_manager.h"

#include <include/cef_app.h>
#include <include/cef_browser.h>
#include <include/wrapper/cef_helpers.h>

#include <cstdio>
#include <mutex>

#if defined(__APPLE__)
#include <dlfcn.h>
#include <libgen.h>
#include <mach-o/dyld.h>
#elif defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#include <linux/limits.h>
#endif

// ---------------------------------------------------------------------------
// VividCefApp — browser-process CefApp with sensible defaults
// ---------------------------------------------------------------------------

class VividCefApp : public CefApp, public CefBrowserProcessHandler {
public:
    CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
        return this;
    }

    void OnBeforeCommandLineProcessing(const CefString& /*process_type*/,
                                       CefRefPtr<CefCommandLine> cmd) override {
        // Run everything in-process: avoids Mach port rendezvous failures
        // when CEF is loaded as a plugin via dlopen in a host app.
        cmd->AppendSwitch("single-process");
        cmd->AppendSwitch("disable-gpu");
        cmd->AppendSwitch("disable-gpu-compositing");
        cmd->AppendSwitch("use-mock-keychain");
        cmd->AppendSwitch("disable-extensions");
        cmd->AppendSwitch("disable-spell-checking");
        cmd->AppendSwitchWithValue("log-severity", "warning");
    }

    void OnContextInitialized() override {}

private:
    IMPLEMENT_REFCOUNTING(VividCefApp);
};

// ---------------------------------------------------------------------------
// Static state
// ---------------------------------------------------------------------------

std::atomic<int>      CefManager::s_refcount{0};
std::atomic<bool>     CefManager::s_initialized{false};
std::atomic<uint64_t> CefManager::s_last_pumped_frame{UINT64_MAX};

static std::mutex              g_init_mutex;
static CefRefPtr<VividCefApp>  g_cef_app;

// ---------------------------------------------------------------------------
// Helper path: find vivid-cef-helper relative to the plugin dylib
// ---------------------------------------------------------------------------

// Forward-declare the symbol that VIVID_REGISTER generates in browser_op.cpp.
// We use its address with dladdr to locate the plugin dylib on disk.
extern "C" const void* vivid_descriptor();

std::string CefManager::helper_path() {
#if defined(__APPLE__) || defined(__linux__)
    Dl_info info;
    if (dladdr(reinterpret_cast<const void*>(&vivid_descriptor), &info) && info.dli_fname) {
        std::string dylib_path = info.dli_fname;
        auto slash = dylib_path.rfind('/');
        if (slash != std::string::npos) {
            return dylib_path.substr(0, slash) + "/vivid-cef-helper";
        }
    }
    return "vivid-cef-helper";  // fallback: hope it's on PATH
#elif defined(_WIN32)
    HMODULE hm = nullptr;
    GetModuleHandleExA(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCSTR>(&vivid_descriptor), &hm);
    if (hm) {
        char path[MAX_PATH];
        GetModuleFileNameA(hm, path, MAX_PATH);
        std::string full(path);
        auto slash = full.find_last_of("\\/");
        if (slash != std::string::npos)
            return full.substr(0, slash) + "\\vivid-cef-helper.exe";
    }
    return "vivid-cef-helper.exe";
#endif
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

bool CefManager::acquire() {
    std::lock_guard<std::mutex> lock(g_init_mutex);

    if (s_refcount.fetch_add(1) > 0) {
        // Already initialized by another BrowserOp
        return s_initialized.load();
    }

    // First consumer — initialize CEF
    g_cef_app = new VividCefApp();

    CefSettings settings;
    settings.no_sandbox = true;
    settings.windowless_rendering_enabled = true;
    settings.external_message_pump = true;
    settings.persist_session_cookies = false;
    settings.log_severity = LOGSEVERITY_WARNING;

    // Subprocess helper
    std::string helper = helper_path();
    CefString(&settings.browser_subprocess_path).FromString(helper);

#if defined(__APPLE__)
    // CEF framework path (sibling of helper)
    auto slash = helper.rfind('/');
    if (slash != std::string::npos) {
        std::string fw = helper.substr(0, slash) + "/Chromium Embedded Framework.framework";
        CefString(&settings.framework_dir_path).FromString(fw);
    }

    // Temp cache to avoid macOS keychain prompts
    const char* tmp = getenv("TMPDIR");
    std::string cache = std::string(tmp ? tmp : "/tmp/") + "vivid-cef-cache";
    CefString(&settings.root_cache_path).FromString(cache);
#endif

    CefMainArgs args(0, nullptr);

    if (!CefInitialize(args, settings, g_cef_app, nullptr)) {
        std::fprintf(stderr, "[vivid-cef] CefInitialize failed\n");
        g_cef_app = nullptr;
        s_refcount.store(0);
        return false;
    }

    s_initialized.store(true);
    std::fprintf(stderr, "[vivid-cef] CEF initialized (helper: %s)\n", helper.c_str());
    return true;
}

void CefManager::release() {
    std::lock_guard<std::mutex> lock(g_init_mutex);

    int prev = s_refcount.fetch_sub(1);
    if (prev <= 1) {
        // Last consumer — shut down CEF
        if (s_initialized.load()) {
            CefShutdown();
            g_cef_app = nullptr;
            s_initialized.store(false);
            s_refcount.store(0);
            std::fprintf(stderr, "[vivid-cef] CEF shut down\n");
        }
    }
}

void CefManager::pump_once(uint64_t frame) {
    uint64_t expected = s_last_pumped_frame.load();
    if (expected == frame) return;  // already pumped this frame

    if (s_last_pumped_frame.compare_exchange_strong(expected, frame)) {
        if (s_initialized.load()) {
            CefDoMessageLoopWork();
        }
    }
}

bool CefManager::is_initialized() {
    return s_initialized.load();
}
