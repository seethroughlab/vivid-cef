#pragma once

// BrowserOp — GPU operator that renders web content via CEF.
//
// Uses the same staging-texture → blit pattern as MovieFileIn:
// 1. CEF renders offscreen to a CPU buffer (OnPaint callback)
// 2. New pixels are uploaded to a staging WGPUTexture
// 3. A fullscreen blit pass copies the staging texture to the output

#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"

#include "cef_client.h"

#include <include/cef_browser.h>

#include <cstdint>
#include <string>

struct BrowserOp : vivid::OperatorBase {
    static constexpr const char* kName   = "Browser";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_GPU;
    static constexpr bool kTimeDependent = true;  // pump CEF every frame

    // Parameters
    vivid::Param<vivid::TextValue> url        {"url"};
    vivid::Param<vivid::TextValue> stream_id  {"stream_id"};
    vivid::Param<float>           zoom       {"zoom", 1.0f, 0.25f, 4.0f};
    vivid::Param<bool>            transparent{"transparent", false};
    vivid::Param<bool>            audio_capture{"audio_capture", true};
    vivid::Param<int>             frame_rate {"frame_rate", 60, 1, 120};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&url);
        out.push_back(&stream_id);
        out.push_back(&zoom);
        out.push_back(&transparent);
        out.push_back(&audio_capture);
        out.push_back(&frame_rate);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"texture", VIVID_PORT_GPU_TEXTURE, VIVID_PORT_OUTPUT});
    }

    void process(const VividProcessContext* ctx) override;
    void main_thread_update(double time) override;

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

    // GPU resources (same pattern as MovieFileIn)
    WGPURenderPipeline  pipeline_     = nullptr;
    WGPUBindGroupLayout bind_layout_  = nullptr;
    WGPUPipelineLayout  pipe_layout_  = nullptr;
    WGPUShaderModule    shader_       = nullptr;
    WGPUSampler         sampler_      = nullptr;
    WGPUTexture         staging_tex_  = nullptr;
    WGPUTextureView     staging_view_ = nullptr;
    WGPUBindGroup       bind_group_   = nullptr;
    uint32_t            staging_w_    = 0;
    uint32_t            staging_h_    = 0;

    // Browser dimensions (default 1280x720, could be parameterized later)
    static constexpr int kBrowserWidth  = 1280;
    static constexpr int kBrowserHeight = 720;

    bool lazy_init_gpu(VividGpuState* gpu);
    void recreate_staging(VividGpuState* gpu, uint32_t w, uint32_t h);
    void upload_pixels(VividGpuState* gpu, const uint8_t* pixels, uint32_t w, uint32_t h);
    void blit(VividGpuState* gpu);
    void clear_output(VividGpuState* gpu);

    void create_browser();
    void update_url(const std::string& new_url);
};
