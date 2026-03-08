#include "cef_client.h"
#include "browser_audio_bridge.h"
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

VividAudioHandler::VividAudioHandler(std::shared_ptr<AudioRoutingState> state)
    : state_(std::move(state)) {}

bool VividAudioHandler::GetAudioParameters(CefRefPtr<CefBrowser> /*browser*/,
                                           CefAudioParameters& params) {
    // Request Vivid's audio engine format for minimal conversion work.
    params.channel_layout = CEF_CHANNEL_LAYOUT_STEREO;
    params.sample_rate = vivid_cef_audio::kTargetSampleRate;
    params.frames_per_buffer = 256;
    return true;
}

void VividAudioHandler::OnAudioStreamStarted(CefRefPtr<CefBrowser> /*browser*/,
                                             const CefAudioParameters& params,
                                             int channels) {
    if (!state_ || !state_->capture_enabled.load(std::memory_order_relaxed)) return;
    state_->channels.store(channels, std::memory_order_relaxed);
    state_->sample_rate.store(params.sample_rate, std::memory_order_relaxed);
    std::string stream_id = state_->stream_id();
    if (stream_id.empty()) return;
    vivid_cef_audio::producer_stream_started(stream_id, params.sample_rate, channels);
}

void VividAudioHandler::OnAudioStreamPacket(CefRefPtr<CefBrowser> /*browser*/,
                                            const float** data,
                                            int frames,
                                            int64_t pts) {
    if (!state_ || !state_->capture_enabled.load(std::memory_order_relaxed)) return;
    std::string stream_id = state_->stream_id();
    if (stream_id.empty()) return;

    int channels = state_->channels.load(std::memory_order_relaxed);
    int sample_rate = state_->sample_rate.load(std::memory_order_relaxed);
    vivid_cef_audio::producer_push_packet(
        stream_id, data, frames, channels, sample_rate, pts);
}

void VividAudioHandler::OnAudioStreamStopped(CefRefPtr<CefBrowser> /*browser*/) {
    if (!state_) return;
    std::string stream_id = state_->stream_id();
    if (stream_id.empty()) return;
    vivid_cef_audio::producer_stream_stopped(stream_id);
}

void VividAudioHandler::OnAudioStreamError(CefRefPtr<CefBrowser> /*browser*/,
                                           const CefString& message) {
    if (!state_) return;
    std::string stream_id = state_->stream_id();
    if (stream_id.empty()) return;
    vivid_cef_audio::producer_stream_error(stream_id, message.ToString());
}

VividCefClient::VividCefClient(CefRefPtr<VividRenderHandler> handler,
                               const std::string& stream_id,
                               bool audio_capture)
    : handler_(handler) {
    audio_state_ = std::make_shared<AudioRoutingState>();
    audio_state_->set_stream_id(stream_id);
    audio_state_->capture_enabled.store(audio_capture, std::memory_order_relaxed);
    audio_handler_ = new VividAudioHandler(audio_state_);
}

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

void VividCefClient::set_audio_stream_id(const std::string& stream_id) {
    if (!audio_state_) return;
    std::string prev = audio_state_->stream_id();
    if (!prev.empty() && prev != stream_id) {
        vivid_cef_audio::producer_stream_stopped(prev);
    }
    audio_state_->set_stream_id(stream_id);
}

void VividCefClient::set_audio_capture(bool enabled) {
    if (!audio_state_) return;
    if (!enabled) {
        std::string sid = audio_state_->stream_id();
        if (!sid.empty()) vivid_cef_audio::producer_stream_stopped(sid);
    }
    audio_state_->capture_enabled.store(enabled, std::memory_order_relaxed);
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
