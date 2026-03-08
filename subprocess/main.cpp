// vivid-cef-helper — CEF subprocess executable.
//
// CEF uses a multi-process architecture (like Chrome). When the main process
// creates a browser, CEF spawns renderer, GPU, and utility subprocesses.
// This helper is used for all subprocess types — it initializes CEF and enters
// its message loop, then exits when the subprocess is no longer needed.
//
// The subprocess path is configured via CefSettings::browser_subprocess_path.
//
// macOS note: An Info.plist is embedded in this binary (via -sectcreate) so
// that CFBundleGetMainBundle() returns bundle ID "com.vivid.app". This makes
// the Mach port rendezvous lookup use the same name as the parent process
// (which registers "com.vivid.app.MachPortRendezvousServer.<pid>").

#include <include/cef_app.h>
#include <include/cef_command_line.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

// Debug: write process type to /tmp so we can detect what processes are reached
static void debug_record_type(const char* type) {
    char path[256];
    snprintf(path, sizeof(path), "/tmp/vivid_cef_%s.txt", type);
    int fd = open(path, O_WRONLY|O_CREAT|O_TRUNC, 0644);
    if (fd >= 0) { write(fd, "reached\n", 8); close(fd); }
}

// CefApp for the renderer subprocess.
class RendererApp : public CefApp, public CefRenderProcessHandler {
public:
    CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override {
        return this;
    }

private:
    IMPLEMENT_REFCOUNTING(RendererApp);
};

// CefApp for the GPU subprocess.
// On macOS, CEF 120's GPU process tries to launch its own sandbox helper
// subprocess. When loaded as a dlopen'd plugin, that inner subprocess fails
// the Mach port rendezvous and the GPU process panics. Adding --no-sandbox
// here (in the GPU process's own command line processing) prevents the GPU
// process from attempting to launch its sandbox subprocess.
class GpuApp : public CefApp {
public:
    void OnBeforeCommandLineProcessing(const CefString& /*process_type*/,
                                       CefRefPtr<CefCommandLine> cmd) override {
        cmd->AppendSwitch("no-sandbox");
        cmd->AppendSwitch("disable-gpu-sandbox");
    }

private:
    IMPLEMENT_REFCOUNTING(GpuApp);
};

static CefRefPtr<CefApp> create_app(const std::string& process_type) {
    if (process_type == "renderer")
        return new RendererApp();
    if (process_type == "gpu-process")
        return new GpuApp();
    return nullptr;
}

#if defined(_WIN32)
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
    CefMainArgs args(hInstance);
    CefRefPtr<CefCommandLine> cmd = CefCommandLine::CreateCommandLine();
    cmd->InitFromString(GetCommandLineW());
#else
int main(int argc, char* argv[]) {
    CefMainArgs args(argc, argv);
    CefRefPtr<CefCommandLine> cmd = CefCommandLine::CreateCommandLine();
    cmd->InitFromArgv(argc, argv);
#endif

    std::string type = cmd->GetSwitchValue("type").ToString();
    debug_record_type(type.empty() ? "unknown" : type.c_str());
    CefRefPtr<CefApp> app = create_app(type);
    return CefExecuteProcess(args, app, nullptr);
}
