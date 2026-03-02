#pragma once

// VividCefClient + VividRenderHandler — CEF browser client for offscreen rendering.
//
// VividRenderHandler receives OnPaint callbacks with BGRA pixel data.
// VividCefClient routes handler interfaces and manages browser lifecycle.

#include <include/cef_client.h>
#include <include/cef_browser.h>
#include <include/cef_render_handler.h>
#include <include/cef_life_span_handler.h>
#include <include/cef_load_handler.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Render handler — receives pixel data from CEF
// ---------------------------------------------------------------------------

class VividRenderHandler : public CefRenderHandler {
public:
    VividRenderHandler(int width, int height);

    // CefRenderHandler
    void GetViewRect(CefRefPtr<CefBrowser> browser, CefRect& rect) override;
    void OnPaint(CefRefPtr<CefBrowser> browser, PaintElementType type,
                 const RectList& dirtyRects, const void* buffer,
                 int width, int height) override;

    // Configuration
    void set_size(int w, int h);

    // Pixel readback
    bool              has_new_frame() const { return new_frame_.load(); }
    void              clear_new_frame()     { new_frame_.store(false); }
    const uint8_t*    pixels() const        { return pixels_.data(); }
    int               pixel_width() const   { return px_w_; }
    int               pixel_height() const  { return px_h_; }
    std::mutex&       pixel_mutex()         { return mutex_; }

private:
    int                    width_;
    int                    height_;
    std::atomic<bool>      new_frame_{false};
    std::vector<uint8_t>   pixels_;
    int                    px_w_ = 0;
    int                    px_h_ = 0;
    std::mutex             mutex_;

    IMPLEMENT_REFCOUNTING(VividRenderHandler);
};

// ---------------------------------------------------------------------------
// Client — owns render handler, manages browser lifecycle
// ---------------------------------------------------------------------------

class VividCefClient : public CefClient,
                       public CefLifeSpanHandler,
                       public CefLoadHandler {
public:
    explicit VividCefClient(CefRefPtr<VividRenderHandler> handler);

    // CefClient
    CefRefPtr<CefRenderHandler>  GetRenderHandler() override   { return handler_; }
    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
    CefRefPtr<CefLoadHandler>    GetLoadHandler() override     { return this; }

    // CefLifeSpanHandler
    void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
    bool DoClose(CefRefPtr<CefBrowser> browser) override;
    void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;

    // CefLoadHandler
    void OnLoadEnd(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                   int httpStatusCode) override;
    void OnLoadError(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                     ErrorCode errorCode, const CefString& errorText,
                     const CefString& failedUrl) override;

    // Accessors
    CefRefPtr<CefBrowser> browser() const { return browser_; }
    bool   is_loading() const { return loading_.load(); }
    void   set_pending_url(const std::string& url) { pending_url_ = url; }

private:
    CefRefPtr<VividRenderHandler> handler_;
    CefRefPtr<CefBrowser>         browser_;
    std::atomic<bool>             loading_{false};
    std::string                   pending_url_;

    IMPLEMENT_REFCOUNTING(VividCefClient);
};
