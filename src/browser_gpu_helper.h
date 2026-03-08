#pragma once

#include "operator_api/gpu_common.h"

#include <cstdint>

class BrowserGpuHelper {
public:
    bool ensure_initialized(VividGpuState* gpu);
    void reset();

    void upload_frame(VividGpuState* gpu, const uint8_t* pixels, uint32_t w, uint32_t h);
    void set_preferred_size(VividProcessContext* ctx) const;
    void render_or_clear(VividGpuState* gpu);
    void clear_output(VividGpuState* gpu);

private:
    void recreate_staging(VividGpuState* gpu, uint32_t w, uint32_t h);
    void upload_pixels(VividGpuState* gpu, const uint8_t* pixels, uint32_t w, uint32_t h);

    vivid::gpu::PipelineHandle pipeline_;
    vivid::gpu::BindLayoutHandle bind_layout_;
    vivid::gpu::PipeLayoutHandle pipe_layout_;
    vivid::gpu::ShaderHandle shader_;
    vivid::gpu::SamplerHandle sampler_;
    vivid::gpu::TextureHandle staging_tex_;
    vivid::gpu::TexViewHandle staging_view_;
    vivid::gpu::BindGroupHandle bind_group_;
    uint32_t staging_w_ = 0;
    uint32_t staging_h_ = 0;
};

