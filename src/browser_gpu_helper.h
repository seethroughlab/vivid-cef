#pragma once

#include "operator_api/gpu_common.h"
#include "operator_api/gpu_operator.h"

#include <cstdint>

class BrowserGpuHelper {
public:
    bool ensure_initialized(const VividGpuContext* gpu);
    void reset();

    void upload_frame(const VividGpuContext* gpu, const uint8_t* pixels, uint32_t w, uint32_t h);
    void set_preferred_size(const VividGpuContext* ctx) const;
    void render_or_clear(const VividGpuContext* gpu);
    void clear_output(const VividGpuContext* gpu);

private:
    void recreate_staging(const VividGpuContext* gpu, uint32_t w, uint32_t h);
    void upload_pixels(const VividGpuContext* gpu, const uint8_t* pixels, uint32_t w, uint32_t h);

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

