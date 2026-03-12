#include "browser_op.h"

#include "browser_input_dispatch.h"
#include "browser_audio_bridge.h"
#include "browser_url_utils.h"
#include "cef_manager.h"

#include <include/cef_browser.h>

#include <cmath>
#include <cstdio>
#include <mutex>

namespace {

bool (*g_test_acquire_hook)() = nullptr;
bool g_test_disable_browser_create = false;
bool g_test_skip_gpu_init = false;
int g_test_create_browser_attempts = 0;

bool try_acquire_cef() {
    if (g_test_acquire_hook) return g_test_acquire_hook();
    return CefManager::acquire();
}

}  // namespace

BrowserOp::BrowserOp() {
    vivid::semantic_tag(url, "url");
    vivid::semantic_intent(url, "web_url_or_file");

    vivid::semantic_tag(zoom, "scale_xy");
    vivid::semantic_shape(zoom, "scalar");
    vivid::semantic_intent(zoom, "view_zoom");

    vivid::semantic_tag(stream_id, "id_stream");
    vivid::semantic_shape(stream_id, "string");

    vivid::semantic_tag(audio_capture, "enabled");
    vivid::semantic_shape(audio_capture, "bool");

    vivid::semantic_tag(frame_rate, "frequency_hz");
    vivid::semantic_shape(frame_rate, "int");
    vivid::semantic_unit(frame_rate, "Hz");

    vivid::param_group(input_x, "Input");
    vivid::param_group(input_y, "Input");
    vivid::param_group(input_w, "Input");
    vivid::param_group(input_h, "Input");
    vivid::param_group(input_focus, "Input");
    vivid::layout_row(input_x, 2, 0);
    vivid::layout_row(input_y, 2, 1);
    vivid::layout_row(input_w, 2, 0);
    vivid::layout_row(input_h, 2, 1);

    cef_gate_.set_acquire_fn([]() { return try_acquire_cef(); });
}

BrowserOp::~BrowserOp() {
    if (client_ && client_->browser()) {
        client_->set_audio_capture(false);
        client_->set_audio_stream_id("");
        client_->browser()->GetHost()->CloseBrowser(true);
    }
    client_ = nullptr;
    render_handler_ = nullptr;
    gpu_helper_.reset();

    if (cef_acquired_) {
        CefManager::release();
    }
}

void BrowserOp::set_test_acquire_hook(bool (*hook)()) {
    g_test_acquire_hook = hook;
}

void BrowserOp::set_test_disable_browser_create(bool disable) {
    g_test_disable_browser_create = disable;
}

void BrowserOp::set_test_skip_gpu_init(bool skip) {
    g_test_skip_gpu_init = skip;
}

int BrowserOp::test_create_browser_attempts() {
    return g_test_create_browser_attempts;
}

void BrowserOp::reset_test_state() {
    g_test_acquire_hook = nullptr;
    g_test_disable_browser_create = false;
    g_test_skip_gpu_init = false;
    g_test_create_browser_attempts = 0;
}

void BrowserOp::create_browser() {
    ++g_test_create_browser_attempts;
    if (g_test_disable_browser_create) return;

    if (!stream_id.str_value.empty() && audio_capture.bool_value()) {
        vivid_cef_audio::producer_source_reset(stream_id.str_value);
    }

    render_handler_ = new VividRenderHandler(kBrowserWidth, kBrowserHeight);
    client_ = new VividCefClient(render_handler_, stream_id.str_value,
                                 audio_capture.bool_value());

    CefBrowserSettings browser_settings;
    browser_settings.windowless_frame_rate = frame_rate.int_value();

    if (transparent.value) {
        browser_settings.background_color = CefColorSetARGB(0, 0, 0, 0);
    }

    CefWindowInfo window_info;
#if defined(_WIN32)
    window_info.SetAsWindowless(nullptr);
#elif defined(__APPLE__)
    window_info.SetAsWindowless(nullptr);
#else
    window_info.SetAsWindowless(0);
#endif
    window_info.shared_texture_enabled = false;

    CefBrowserHost::CreateBrowser(window_info, client_, "", browser_settings, nullptr, nullptr);
}

void BrowserOp::update_url(const std::string& new_url) {
    if (new_url == last_url_) return;
    last_url_ = new_url;
    if (!stream_id.str_value.empty() && audio_capture.bool_value()) {
        vivid_cef_audio::producer_source_reset(stream_id.str_value);
    }
    if (new_url.empty()) return;

    std::string resolved = resolve_browser_url(new_url, graph_base_dir_);
    if (!client_) create_browser();
    if (!client_) return;

    if (client_->browser()) {
        auto frame = client_->browser()->GetMainFrame();
        if (frame) frame->LoadURL(resolved);
    } else {
        client_->set_pending_url(resolved);
    }
}

void BrowserOp::main_thread_update(double /*time*/) {}

void BrowserOp::process_gpu(const VividGpuContext* ctx) {
    if (!cef_acquired_) {
        if (!cef_gate_.ensure_acquired()) {
            if (cef_gate_.consume_should_log_failure()) {
                std::fprintf(stderr,
                             "[vivid-cef] BrowserOp: CEF acquire failed; retrying next frame\n");
            }
            gpu_helper_.clear_output(ctx);
            return;
        }
        cef_acquired_ = true;
    }

    if (!g_test_skip_gpu_init && !gpu_helper_.ensure_initialized(ctx)) {
        std::fprintf(stderr, "[vivid-cef] GPU init failed\n");
        return;
    }

    CefManager::pump_once(ctx->frame);

    if (!client_) create_browser();

    update_url(url.str_value);

    if (client_) {
        if (stream_id.str_value != last_stream_id_) {
            last_stream_id_ = stream_id.str_value;
            client_->set_audio_stream_id(last_stream_id_);
        }
        bool capture = audio_capture.bool_value();
        if (capture != last_audio_capture_) {
            last_audio_capture_ = capture;
            client_->set_audio_capture(capture);
        }
    }

    if (frame_rate.int_value() != last_frame_rate_) {
        last_frame_rate_ = frame_rate.int_value();
        if (client_ && client_->browser()) {
            client_->browser()->GetHost()->SetWindowlessFrameRate(last_frame_rate_);
        }
    }

    if (zoom.value != last_zoom_) {
        last_zoom_ = zoom.value;
        if (client_ && client_->browser()) {
            double cef_zoom = std::log(static_cast<double>(zoom.value)) / std::log(1.2);
            client_->browser()->GetHost()->SetZoomLevel(cef_zoom);
        }
    }

    BrowserInputViewport vp{input_x.value, input_y.value, input_w.value, input_h.value,
                            input_focus.bool_value()};
    forward_browser_input_events_viewport(client_, ctx->input, kBrowserWidth, kBrowserHeight,
                                          vp, mouse_was_inside_);

    if (render_handler_ && render_handler_->has_new_frame()) {
        std::lock_guard<std::mutex> lock(render_handler_->pixel_mutex());
        const uint8_t* pixels = render_handler_->pixels();
        uint32_t w = static_cast<uint32_t>(render_handler_->pixel_width());
        uint32_t h = static_cast<uint32_t>(render_handler_->pixel_height());
        gpu_helper_.upload_frame(ctx, pixels, w, h);
        render_handler_->clear_new_frame();
    }

    gpu_helper_.set_preferred_size(ctx);
    gpu_helper_.render_or_clear(ctx);
}

VIVID_REGISTER(BrowserOp)
