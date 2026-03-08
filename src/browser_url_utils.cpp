#include "browser_url_utils.h"

#include "operator_api/types.h"

#include <filesystem>
#include <vector>

#if defined(__APPLE__) || defined(__linux__)
#include <dlfcn.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

extern "C" const VividOperatorDescriptor* vivid_descriptor();

namespace {

std::string plugin_dir_for_fallback() {
#if defined(__APPLE__) || defined(__linux__)
    Dl_info info;
    if (dladdr(reinterpret_cast<const void*>(&vivid_descriptor), &info) && info.dli_fname) {
        std::filesystem::path p(info.dli_fname);
        return p.parent_path().string();
    }
#elif defined(_WIN32)
    HMODULE hm = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(&vivid_descriptor), &hm)) {
        char buf[MAX_PATH];
        DWORD n = GetModuleFileNameA(hm, buf, MAX_PATH);
        if (n > 0 && n < MAX_PATH) {
            std::filesystem::path p(buf);
            return p.parent_path().string();
        }
    }
#endif
    return std::filesystem::current_path().string();
}

std::filesystem::path resolve_relative_file_path(const std::string& rel,
                                                 const std::string& graph_base_dir) {
    std::vector<std::filesystem::path> candidates;

    if (!graph_base_dir.empty()) {
        std::filesystem::path gb(graph_base_dir);
        if (gb.is_absolute()) candidates.push_back(gb);
    }

    const std::filesystem::path plugin_dir = plugin_dir_for_fallback();
    candidates.push_back(plugin_dir);
    candidates.push_back(plugin_dir / "..");
    candidates.push_back(std::filesystem::current_path());

    for (const auto& base_raw : candidates) {
        std::error_code ec;
        std::filesystem::path base = std::filesystem::weakly_canonical(base_raw, ec);
        if (ec) base = base_raw.lexically_normal();
        std::filesystem::path candidate = (base / rel).lexically_normal();
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
    }

    return (candidates.front() / rel).lexically_normal();
}

}  // namespace

std::string resolve_browser_url(const std::string& raw_url,
                                const std::string& graph_base_dir) {
    if (raw_url.empty()) return raw_url;
    std::string resolved = raw_url;
    if (resolved.rfind("file://", 0) == 0) {
        std::string file_path = resolved.substr(7);
        if (!file_path.empty() && file_path[0] != '/') {
            file_path = resolve_relative_file_path(file_path, graph_base_dir).string();
            resolved = "file://" + file_path;
        }
    } else if (resolved.find("://") == std::string::npos) {
        if (!resolved.empty() && resolved[0] != '/') {
            resolved = resolve_relative_file_path(resolved, graph_base_dir).string();
        }
        resolved = "file://" + resolved;
    }
    return resolved;
}
