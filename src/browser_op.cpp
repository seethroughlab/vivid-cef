#include "browser_op.h"
#include "cef_manager.h"
#include "operator_api/gpu_common.h"

#include <include/cef_browser.h>

#include <cstdio>
#include <cstring>
#include <vector>

// =============================================================================
// Blit WGSL — identical to MovieFileIn's blit shader
// =============================================================================

static const char* kBlitFragment = R"(

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@group(0) @binding(0) var texSampler: sampler;
@group(0) @binding(1) var tex: texture_2d<f32>;

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    let fs = fullscreenTriangle(vertexIndex, true);
    var out: VertexOutput;
    out.position = fs.position;
    out.uv = fs.uv;
    return out;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    return textureSample(tex, texSampler, input.uv);
}
)";

// =============================================================================
// Constructor / Destructor — CEF lifecycle via CefManager
// =============================================================================

BrowserOp::BrowserOp() {
    // Note: CefManager::acquire() is NOT called here.
    // The constructor runs during descriptor probing (VIVID_REGISTER macro),
    // where the dylib is loaded, probed, and immediately dlclose'd.
    // CEF cannot survive init → shutdown → re-init in the same process,
    // so we defer initialization to the first process() call instead.
}

BrowserOp::~BrowserOp() {
    // Close browser before releasing GPU resources
    if (client_ && client_->browser()) {
        client_->browser()->GetHost()->CloseBrowser(true);
    }
    client_     = nullptr;
    render_handler_ = nullptr;

    // Release GPU resources
    vivid::gpu::release(pipeline_);
    vivid::gpu::release(bind_layout_);
    vivid::gpu::release(pipe_layout_);
    vivid::gpu::release(shader_);
    vivid::gpu::release(sampler_);
    vivid::gpu::release(staging_tex_);
    vivid::gpu::release(staging_view_);
    vivid::gpu::release(bind_group_);

    if (cef_acquired_) {
        CefManager::release();
    }
}

// =============================================================================
// Browser creation and URL management
// =============================================================================

void BrowserOp::create_browser() {
    render_handler_ = new VividRenderHandler(kBrowserWidth, kBrowserHeight);
    client_         = new VividCefClient(render_handler_);

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
    window_info.shared_texture_enabled = false;  // Use CPU path (OnPaint)

    CefBrowserHost::CreateBrowser(
        window_info, client_, "", browser_settings, nullptr, nullptr);

    std::fprintf(stderr, "[vivid-cef] Browser created (%dx%d, %d fps)\n",
                 kBrowserWidth, kBrowserHeight, frame_rate.int_value());
}

void BrowserOp::update_url(const std::string& new_url) {
    if (new_url == last_url_) return;
    last_url_ = new_url;

    if (new_url.empty()) return;

    if (!client_) {
        create_browser();
    }

    if (client_->browser()) {
        auto frame = client_->browser()->GetMainFrame();
        if (frame) {
            frame->LoadURL(new_url);
            std::fprintf(stderr, "[vivid-cef] Loading: %s\n", new_url.c_str());
        }
    } else {
        client_->set_pending_url(new_url);
    }
}

// =============================================================================
// main_thread_update — pump CEF (called on main thread)
// =============================================================================

void BrowserOp::main_thread_update(double /*time*/) {
    // CefDoMessageLoopWork must be called on the main thread.
    // CefManager::pump_once dedupes across all BrowserOps per frame.
    // Note: main_thread_update doesn't receive a frame number, but
    // process() also pumps with the real frame counter.
}

// =============================================================================
// process — GPU operator entry point
// =============================================================================

void BrowserOp::process(const VividProcessContext* ctx) {
    VividGpuState* gpu = vivid_gpu(ctx);
    if (!gpu) return;

    // Lazy-init CEF (deferred from constructor to avoid probe/dlclose issues)
    if (!cef_acquired_) {
        CefManager::acquire();
        cef_acquired_ = true;
    }

    // Lazy-init GPU pipeline
    if (!pipeline_) {
        if (!lazy_init_gpu(gpu)) {
            std::fprintf(stderr, "[vivid-cef] GPU init failed\n");
            return;
        }
    }

    // Pump CEF message loop (once per frame across all BrowserOps)
    CefManager::pump_once(ctx->frame);

    // Create browser lazily (needs CEF to be initialized first)
    if (!client_) {
        create_browser();
    }

    // Check for URL changes
    update_url(url.str_value);

    // Update frame rate if changed
    if (frame_rate.int_value() != last_frame_rate_) {
        last_frame_rate_ = frame_rate.int_value();
        if (client_ && client_->browser()) {
            client_->browser()->GetHost()->SetWindowlessFrameRate(last_frame_rate_);
        }
    }

    // Update zoom if changed
    if (zoom.value != last_zoom_) {
        last_zoom_ = zoom.value;
        if (client_ && client_->browser()) {
            // CEF zoom is logarithmic: 0 = 100%, positive = zoom in
            double cef_zoom = std::log(static_cast<double>(zoom.value)) / std::log(1.2);
            client_->browser()->GetHost()->SetZoomLevel(cef_zoom);
        }
    }

    // Check for new pixels from CEF's OnPaint callback
    if (render_handler_ && render_handler_->has_new_frame()) {
        std::lock_guard<std::mutex> lock(render_handler_->pixel_mutex());

        const uint8_t* pixels = render_handler_->pixels();
        uint32_t w = static_cast<uint32_t>(render_handler_->pixel_width());
        uint32_t h = static_cast<uint32_t>(render_handler_->pixel_height());

        if (pixels && w > 0 && h > 0) {
            if (w != staging_w_ || h != staging_h_) {
                recreate_staging(gpu, w, h);
            }
            upload_pixels(gpu, pixels, w, h);
        }

        render_handler_->clear_new_frame();
    }

    // Request output texture to match browser dimensions
    if (staging_w_ > 0 && staging_h_ > 0) {
        auto* mctx = const_cast<VividProcessContext*>(ctx);
        mctx->preferred_tex_width  = staging_w_;
        mctx->preferred_tex_height = staging_h_;
    }

    // Blit staging texture to output (or clear if nothing loaded)
    if (staging_view_ && bind_group_) {
        blit(gpu);
    } else {
        clear_output(gpu);
    }
}

// =============================================================================
// GPU helpers — same pattern as MovieFileIn
// =============================================================================

bool BrowserOp::lazy_init_gpu(VividGpuState* gpu) {
    shader_ = vivid::gpu::create_shader(gpu->device, kBlitFragment, "Browser Shader");
    if (!shader_) return false;

    sampler_ = vivid::gpu::create_linear_sampler(gpu->device, "Browser Sampler");

    // Bind group layout: sampler(0) + texture(1)
    WGPUBindGroupLayoutEntry entries[2]{};
    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Fragment;
    entries[0].sampler.type = WGPUSamplerBindingType_Filtering;

    entries[1].binding = 1;
    entries[1].visibility = WGPUShaderStage_Fragment;
    entries[1].texture.sampleType = WGPUTextureSampleType_Float;
    entries[1].texture.viewDimension = WGPUTextureViewDimension_2D;
    entries[1].texture.multisampled = false;

    WGPUBindGroupLayoutDescriptor bgl_desc{};
    bgl_desc.label = vivid_sv("Browser BGL");
    bgl_desc.entryCount = 2;
    bgl_desc.entries = entries;
    bind_layout_ = wgpuDeviceCreateBindGroupLayout(gpu->device, &bgl_desc);

    WGPUPipelineLayoutDescriptor pl_desc{};
    pl_desc.label = vivid_sv("Browser Pipeline Layout");
    pl_desc.bindGroupLayoutCount = 1;
    pl_desc.bindGroupLayouts = &bind_layout_;
    pipe_layout_ = wgpuDeviceCreatePipelineLayout(gpu->device, &pl_desc);

    pipeline_ = vivid::gpu::create_pipeline(
        gpu->device, shader_, pipe_layout_, gpu->output_format, "Browser Pipeline");
    return pipeline_ != nullptr;
}

void BrowserOp::recreate_staging(VividGpuState* gpu, uint32_t w, uint32_t h) {
    vivid::gpu::release(staging_tex_);
    vivid::gpu::release(staging_view_);
    vivid::gpu::release(bind_group_);

    staging_w_ = w;
    staging_h_ = h;

    // CEF provides BGRA pixels
    static constexpr WGPUTextureFormat kFmt = WGPUTextureFormat_BGRA8Unorm;

    WGPUTextureDescriptor td{};
    td.label = vivid_sv("Browser Staging");
    td.size = { w, h, 1 };
    td.mipLevelCount = 1;
    td.sampleCount = 1;
    td.dimension = WGPUTextureDimension_2D;
    td.format = kFmt;
    td.usage = WGPUTextureUsage_CopyDst | WGPUTextureUsage_TextureBinding;
    staging_tex_ = wgpuDeviceCreateTexture(gpu->device, &td);

    WGPUTextureViewDescriptor vd{};
    vd.format = kFmt;
    vd.dimension = WGPUTextureViewDimension_2D;
    vd.mipLevelCount = 1;
    vd.arrayLayerCount = 1;
    vd.aspect = WGPUTextureAspect_All;
    staging_view_ = wgpuTextureCreateView(staging_tex_, &vd);

    // Recreate bind group with new texture view
    WGPUBindGroupEntry bg_entries[2]{};
    bg_entries[0].binding = 0;
    bg_entries[0].sampler = sampler_;
    bg_entries[1].binding = 1;
    bg_entries[1].textureView = staging_view_;

    WGPUBindGroupDescriptor bg_desc{};
    bg_desc.label = vivid_sv("Browser BG");
    bg_desc.layout = bind_layout_;
    bg_desc.entryCount = 2;
    bg_desc.entries = bg_entries;
    bind_group_ = wgpuDeviceCreateBindGroup(gpu->device, &bg_desc);
}

void BrowserOp::upload_pixels(VividGpuState* gpu, const uint8_t* pixels,
                               uint32_t w, uint32_t h) {
    if (!staging_tex_) return;

    uint32_t src_bpr = w * 4;
    // WebGPU requires bytesPerRow to be a multiple of 256
    uint32_t aligned_bpr = (src_bpr + 255) & ~255u;

    WGPUTexelCopyTextureInfo dest{};
    dest.texture = staging_tex_;
    dest.mipLevel = 0;
    dest.origin = { 0, 0, 0 };
    dest.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyBufferLayout layout{};
    layout.bytesPerRow = aligned_bpr;
    layout.rowsPerImage = h;

    WGPUExtent3D extent = { w, h, 1 };

    if (aligned_bpr == src_bpr) {
        wgpuQueueWriteTexture(gpu->queue, &dest, pixels,
                              static_cast<size_t>(src_bpr) * h, &layout, &extent);
    } else {
        std::vector<uint8_t> padded(static_cast<size_t>(aligned_bpr) * h, 0);
        for (uint32_t row = 0; row < h; ++row) {
            std::memcpy(padded.data() + row * aligned_bpr,
                       pixels + row * src_bpr, src_bpr);
        }
        wgpuQueueWriteTexture(gpu->queue, &dest, padded.data(),
                              padded.size(), &layout, &extent);
    }
}

void BrowserOp::blit(VividGpuState* gpu) {
    vivid::gpu::run_pass(gpu->command_encoder, pipeline_, bind_group_,
                         gpu->output_texture_view, "Browser Blit");
}

void BrowserOp::clear_output(VividGpuState* gpu) {
    if (!gpu->output_texture_view) return;

    WGPURenderPassColorAttachment color_att{};
    color_att.view = gpu->output_texture_view;
    color_att.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    color_att.resolveTarget = nullptr;
    color_att.loadOp  = WGPULoadOp_Clear;
    color_att.storeOp = WGPUStoreOp_Store;
    color_att.clearValue = { 0.0, 0.0, 0.0, 1.0 };

    WGPURenderPassDescriptor rp_desc{};
    rp_desc.label = vivid_sv("Browser Clear");
    rp_desc.colorAttachmentCount = 1;
    rp_desc.colorAttachments = &color_att;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(
        gpu->command_encoder, &rp_desc);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
}

// =============================================================================
// Registration
// =============================================================================

VIVID_REGISTER(BrowserOp)
