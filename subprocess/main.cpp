// vivid-cef-helper — CEF subprocess executable.
//
// CEF uses a multi-process architecture (like Chrome). When the main process
// creates a browser, CEF spawns renderer, GPU, and utility subprocesses.
// This helper is used for all subprocess types — it initializes CEF and enters
// its message loop, then exits when the subprocess is no longer needed.
//
// The subprocess path is configured via CefSettings::browser_subprocess_path.

#include <include/cef_app.h>
#include <include/cef_command_line.h>

#if defined(_WIN32)
#include <windows.h>
#endif

// Minimal CefApp for subprocess types that need one.
class SubprocessApp : public CefApp, public CefRenderProcessHandler {
public:
    CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override {
        return this;
    }

private:
    IMPLEMENT_REFCOUNTING(SubprocessApp);
};

static CefRefPtr<CefApp> create_app(const std::string& process_type) {
    if (process_type == "renderer")
        return new SubprocessApp();
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
    CefRefPtr<CefApp> app = create_app(type);
    return CefExecuteProcess(args, app, nullptr);
}
