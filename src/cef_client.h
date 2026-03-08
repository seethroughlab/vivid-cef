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
#include <include/cef_display_handler.h>
#include <include/cef_audio_handler.h>

#include <atomic>
#include <cstdint>
#include <memory>
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
// Shared audio routing config between BrowserOp and CEF audio callbacks
// ---------------------------------------------------------------------------

struct AudioRoutingState {
    std::atomic<bool> capture_enabled{true};
    std::atomic<int> channels{2};
    std::atomic<int> sample_rate{48000};

    void set_stream_id(const std::string& id) {
        std::lock_guard<std::mutex> lock(mutex_);
        stream_id_ = id;
    }

    std::string stream_id() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return stream_id_;
    }

private:
    mutable std::mutex mutex_;
    std::string stream_id_;
};

// ---------------------------------------------------------------------------
// Audio handler — receives PCM packets from CEF
// ---------------------------------------------------------------------------

class VividAudioHandler : public CefAudioHandler {
public:
    explicit VividAudioHandler(std::shared_ptr<AudioRoutingState> state);

    bool GetAudioParameters(CefRefPtr<CefBrowser> browser,
                            CefAudioParameters& params) override;
    void OnAudioStreamStarted(CefRefPtr<CefBrowser> browser,
                              const CefAudioParameters& params,
                              int channels) override;
    void OnAudioStreamPacket(CefRefPtr<CefBrowser> browser,
                             const float** data,
                             int frames,
                             int64_t pts) override;
    void OnAudioStreamStopped(CefRefPtr<CefBrowser> browser) override;
    void OnAudioStreamError(CefRefPtr<CefBrowser> browser,
                            const CefString& message) override;

private:
    std::shared_ptr<AudioRoutingState> state_;

    IMPLEMENT_REFCOUNTING(VividAudioHandler);
};

// ---------------------------------------------------------------------------
// Client — owns render handler, manages browser lifecycle
// ---------------------------------------------------------------------------

class VividCefClient : public CefClient,
                       public CefLifeSpanHandler,
                       public CefLoadHandler,
                       public CefDisplayHandler {
public:
    explicit VividCefClient(CefRefPtr<VividRenderHandler> handler,
                            const std::string& stream_id,
                            bool audio_capture);

    // CefClient
    CefRefPtr<CefRenderHandler>   GetRenderHandler() override   { return handler_; }
    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
    CefRefPtr<CefLoadHandler>     GetLoadHandler() override     { return this; }
    CefRefPtr<CefDisplayHandler>  GetDisplayHandler() override  { return this; }
    CefRefPtr<CefAudioHandler>    GetAudioHandler() override    { return audio_handler_; }

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

    // CefDisplayHandler — forward console messages to stderr for debugging
    bool OnConsoleMessage(CefRefPtr<CefBrowser> browser, cef_log_severity_t level,
                          const CefString& message, const CefString& source,
                          int line) override;

    // Accessors
    CefRefPtr<CefBrowser> browser() const { return browser_; }
    bool   is_loading() const { return loading_.load(); }
    void   set_pending_url(const std::string& url) { pending_url_ = url; }
    void   set_audio_stream_id(const std::string& stream_id);
    void   set_audio_capture(bool enabled);

private:
    CefRefPtr<VividRenderHandler> handler_;
    CefRefPtr<VividAudioHandler>  audio_handler_;
    std::shared_ptr<AudioRoutingState> audio_state_;
    CefRefPtr<CefBrowser>         browser_;
    std::atomic<bool>             loading_{false};
    std::string                   pending_url_;

    IMPLEMENT_REFCOUNTING(VividCefClient);
};
