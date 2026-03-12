#pragma once

// BrowserOp — GPU operator that renders web content via CEF.
//
// Uses the same staging-texture → blit pattern as MovieFileIn:
// 1. CEF renders offscreen to a CPU buffer (OnPaint callback)
// 2. New pixels are uploaded to a staging WGPUTexture
// 3. A fullscreen blit pass copies the staging texture to the output

#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"

#include "browser_cef_gate.h"
#include "browser_gpu_helper.h"
#include "cef_client.h"

#include <include/cef_browser.h>

#include <cstdint>
#include <string>

struct BrowserOp : vivid::GpuOperatorBase {
    static constexpr const char* kName   = "Browser";
    static constexpr bool kTimeDependent = true;  // pump CEF every frame

    // Parameters
    vivid::Param<vivid::TextValue> url        {"url"};
    vivid::Param<vivid::TextValue> stream_id  {"stream_id"};
    vivid::Param<float>           zoom       {"zoom", 1.0f, 0.25f, 4.0f};
    vivid::Param<bool>            transparent{"transparent", false};
    vivid::Param<bool>            audio_capture{"audio_capture", true};
    vivid::Param<int>             frame_rate {"frame_rate", 60, 1, 120};
    vivid::Param<float>           input_x    {"input_x", 0.0f, 0.0f, 1.0f};
    vivid::Param<float>           input_y    {"input_y", 0.0f, 0.0f, 1.0f};
    vivid::Param<float>           input_w    {"input_w", 1.0f, 0.0f, 1.0f};
    vivid::Param<float>           input_h    {"input_h", 1.0f, 0.0f, 1.0f};
    vivid::Param<bool>            input_focus{"input_focus", true};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&url);
        out.push_back(&stream_id);
        out.push_back(&zoom);
        out.push_back(&transparent);
        out.push_back(&audio_capture);
        out.push_back(&frame_rate);
        out.push_back(&input_x);
        out.push_back(&input_y);
        out.push_back(&input_w);
        out.push_back(&input_h);
        out.push_back(&input_focus);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"texture", VIVID_PORT_TEXTURE, VIVID_PORT_OUTPUT});
    }

    void process_gpu(const VividGpuContext* ctx) override;
    void main_thread_update(double time) override;
    static void set_test_acquire_hook(bool (*hook)());
    static void set_test_disable_browser_create(bool disable);
    static void set_test_skip_gpu_init(bool skip);
    static int test_create_browser_attempts();
    static void reset_test_state();

    BrowserOp();
    ~BrowserOp() override;

private:
    // CEF state
    bool                          cef_acquired_ = false;
    CefRefPtr<VividRenderHandler> render_handler_;
    CefRefPtr<VividCefClient>     client_;
    std::string                   last_url_;
    std::string                   graph_base_dir_;
    std::string                   last_stream_id_;
    bool                          last_audio_capture_ = true;
    int                           last_frame_rate_ = 60;
    float                         last_zoom_ = 1.0f;
    BrowserCefGate                cef_gate_;
    BrowserGpuHelper              gpu_helper_;
    bool                          mouse_was_inside_ = false;

    // Browser dimensions (default 1280x720, could be parameterized later)
    static constexpr int kBrowserWidth  = 1280;
    static constexpr int kBrowserHeight = 720;

    void create_browser();
    void update_url(const std::string& new_url);
};
