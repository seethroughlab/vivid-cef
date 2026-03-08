#include "browser_gpu_helper.h"

#include <cstring>
#include <vector>

namespace {

const char* kBlitFragment = R"(

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

}  // namespace

bool BrowserGpuHelper::ensure_initialized(VividGpuState* gpu) {
    if (pipeline_) return true;
    shader_.reset(vivid::gpu::create_shader(gpu->device, kBlitFragment, "Browser Shader"));
    if (!shader_) return false;
    sampler_.reset(vivid::gpu::create_linear_sampler(gpu->device, "Browser Sampler"));

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
    bind_layout_.reset(wgpuDeviceCreateBindGroupLayout(gpu->device, &bgl_desc));

    WGPUPipelineLayoutDescriptor pl_desc{};
    pl_desc.label = vivid_sv("Browser Pipeline Layout");
    pl_desc.bindGroupLayoutCount = 1;
    WGPUBindGroupLayout layout = bind_layout_.get();
    pl_desc.bindGroupLayouts = &layout;
    pipe_layout_.reset(wgpuDeviceCreatePipelineLayout(gpu->device, &pl_desc));

    pipeline_.reset(vivid::gpu::create_pipeline(gpu->device, shader_.get(),
                                                pipe_layout_.get(),
                                                gpu->output_format, "Browser Pipeline"));
    return static_cast<bool>(pipeline_);
}

void BrowserGpuHelper::reset() {
    bind_group_.reset();
    staging_view_.reset();
    staging_tex_.reset();
    sampler_.reset();
    shader_.reset();
    pipeline_.reset();
    pipe_layout_.reset();
    bind_layout_.reset();
    staging_w_ = 0;
    staging_h_ = 0;
}

void BrowserGpuHelper::recreate_staging(VividGpuState* gpu, uint32_t w, uint32_t h) {
    bind_group_.reset();
    staging_view_.reset();
    staging_tex_.reset();

    staging_w_ = w;
    staging_h_ = h;

    static constexpr WGPUTextureFormat kFmt = WGPUTextureFormat_BGRA8Unorm;
    WGPUTextureDescriptor td{};
    td.label = vivid_sv("Browser Staging");
    td.size = { w, h, 1 };
    td.mipLevelCount = 1;
    td.sampleCount = 1;
    td.dimension = WGPUTextureDimension_2D;
    td.format = kFmt;
    td.usage = WGPUTextureUsage_CopyDst | WGPUTextureUsage_TextureBinding;
    staging_tex_.reset(wgpuDeviceCreateTexture(gpu->device, &td));

    WGPUTextureViewDescriptor vd{};
    vd.format = kFmt;
    vd.dimension = WGPUTextureViewDimension_2D;
    vd.mipLevelCount = 1;
    vd.arrayLayerCount = 1;
    vd.aspect = WGPUTextureAspect_All;
    staging_view_.reset(wgpuTextureCreateView(staging_tex_.get(), &vd));

    WGPUBindGroupEntry bg_entries[2]{};
    bg_entries[0].binding = 0;
    bg_entries[0].sampler = sampler_.get();
    bg_entries[1].binding = 1;
    bg_entries[1].textureView = staging_view_.get();

    WGPUBindGroupDescriptor bg_desc{};
    bg_desc.label = vivid_sv("Browser BG");
    bg_desc.layout = bind_layout_.get();
    bg_desc.entryCount = 2;
    bg_desc.entries = bg_entries;
    bind_group_.reset(wgpuDeviceCreateBindGroup(gpu->device, &bg_desc));
}

void BrowserGpuHelper::upload_pixels(VividGpuState* gpu, const uint8_t* pixels, uint32_t w, uint32_t h) {
    if (!staging_tex_) return;
    uint32_t src_bpr = w * 4;
    uint32_t aligned_bpr = (src_bpr + 255) & ~255u;

    WGPUTexelCopyTextureInfo dest{};
    dest.texture = staging_tex_.get();
    dest.mipLevel = 0;
    dest.origin = {0, 0, 0};
    dest.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyBufferLayout layout{};
    layout.bytesPerRow = aligned_bpr;
    layout.rowsPerImage = h;

    WGPUExtent3D extent = {w, h, 1};

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

void BrowserGpuHelper::upload_frame(VividGpuState* gpu, const uint8_t* pixels, uint32_t w, uint32_t h) {
    if (!pixels || w == 0 || h == 0) return;
    if (w != staging_w_ || h != staging_h_) {
        recreate_staging(gpu, w, h);
    }
    upload_pixels(gpu, pixels, w, h);
}

void BrowserGpuHelper::set_preferred_size(VividProcessContext* ctx) const {
    if (!ctx || staging_w_ == 0 || staging_h_ == 0) return;
    ctx->preferred_tex_width = staging_w_;
    ctx->preferred_tex_height = staging_h_;
}

void BrowserGpuHelper::render_or_clear(VividGpuState* gpu) {
    if (staging_view_ && bind_group_) {
        vivid::gpu::run_pass(gpu->command_encoder, pipeline_.get(), bind_group_.get(),
                             gpu->output_texture_view, "Browser Blit");
    } else {
        clear_output(gpu);
    }
}

void BrowserGpuHelper::clear_output(VividGpuState* gpu) {
    if (!gpu || !gpu->output_texture_view) return;
    WGPURenderPassColorAttachment color_att{};
    color_att.view = gpu->output_texture_view;
    color_att.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    color_att.resolveTarget = nullptr;
    color_att.loadOp = WGPULoadOp_Clear;
    color_att.storeOp = WGPUStoreOp_Store;
    color_att.clearValue = {0.0, 0.0, 0.0, 1.0};

    WGPURenderPassDescriptor rp_desc{};
    rp_desc.label = vivid_sv("Browser Clear");
    rp_desc.colorAttachmentCount = 1;
    rp_desc.colorAttachments = &color_att;

    WGPURenderPassEncoder pass =
        wgpuCommandEncoderBeginRenderPass(gpu->command_encoder, &rp_desc);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
}

