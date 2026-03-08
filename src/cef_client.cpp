#include "cef_client.h"
#include <cstdio>
#include <cstring>

// ===========================================================================
// VividRenderHandler
// ===========================================================================

VividRenderHandler::VividRenderHandler(int width, int height)
    : width_(width), height_(height) {}

void VividRenderHandler::GetViewRect(CefRefPtr<CefBrowser> /*browser*/, CefRect& rect) {
    rect.Set(0, 0, width_, height_);
}

void VividRenderHandler::OnPaint(CefRefPtr<CefBrowser> /*browser*/,
                                  PaintElementType type,
                                  const RectList& /*dirtyRects*/,
                                  const void* buffer,
                                  int width, int height) {
    if (type != PET_VIEW) return;

    std::lock_guard<std::mutex> lock(mutex_);
    size_t bytes = static_cast<size_t>(width) * height * 4;
    if (pixels_.size() != bytes)
        pixels_.resize(bytes);

    std::memcpy(pixels_.data(), buffer, bytes);
    px_w_ = width;
    px_h_ = height;
    new_frame_.store(true);
}

void VividRenderHandler::set_size(int w, int h) {
    width_  = w;
    height_ = h;
}

// ===========================================================================
// VividCefClient
// ===========================================================================

VividCefClient::VividCefClient(CefRefPtr<VividRenderHandler> handler)
    : handler_(handler) {}

void VividCefClient::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
    browser_ = browser;

    // Tell CEF the page is visible so rAF isn't throttled in offscreen mode
    browser->GetHost()->WasHidden(false);

    if (!pending_url_.empty()) {
        auto frame = browser->GetMainFrame();
        if (frame)
            frame->LoadURL(pending_url_);
        pending_url_.clear();
    }
}

bool VividCefClient::DoClose(CefRefPtr<CefBrowser> /*browser*/) {
    return false;
}

void VividCefClient::OnBeforeClose(CefRefPtr<CefBrowser> /*browser*/) {
    browser_ = nullptr;
}

void VividCefClient::OnLoadEnd(CefRefPtr<CefBrowser> /*browser*/,
                                CefRefPtr<CefFrame> frame,
                                int /*httpStatusCode*/) {
    if (frame->IsMain())
        loading_.store(false);
}

bool VividCefClient::OnConsoleMessage(CefRefPtr<CefBrowser> /*browser*/,
                                       cef_log_severity_t level,
                                       const CefString& message,
                                       const CefString& source,
                                       int line) {
    std::fprintf(stderr, "[vivid-cef] console[%d] %s:%d: %s\n",
                 level, source.ToString().c_str(), line, message.ToString().c_str());
    return false;  // don't suppress
}

void VividCefClient::OnLoadError(CefRefPtr<CefBrowser> /*browser*/,
                                  CefRefPtr<CefFrame> frame,
                                  ErrorCode errorCode,
                                  const CefString& errorText,
                                  const CefString& failedUrl) {
    if (frame->IsMain()) {
        loading_.store(false);
        std::fprintf(stderr, "[vivid-cef] Load error %d: %s — %s\n",
                     errorCode, errorText.ToString().c_str(),
                     failedUrl.ToString().c_str());
    }
}
