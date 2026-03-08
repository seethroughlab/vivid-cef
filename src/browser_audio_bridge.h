#pragma once

#include <cstdint>
#include <string>

namespace vivid_cef_audio {

static constexpr uint32_t kTargetSampleRate = 48000;

struct StreamStats {
    float    fill_ratio = 0.0f;
    float    drift_ms = 0.0f;
    uint32_t available_frames = 0;
    uint64_t underruns = 0;
    uint64_t overruns = 0;
    uint64_t generation = 0;
    bool     producer_active = false;
    uint32_t producer_sample_rate = 0;
};

void producer_source_reset(const std::string& stream_id);
void producer_stream_started(const std::string& stream_id, int sample_rate, int channels);
void producer_stream_stopped(const std::string& stream_id);
void producer_stream_error(const std::string& stream_id, const std::string& message);
void producer_push_packet(const std::string& stream_id, const float** data,
                          int frames, int channels, int sample_rate, int64_t pts_ms);

bool claim_consumer(const std::string& stream_id, uint64_t consumer_id);
void release_consumer(const std::string& stream_id, uint64_t consumer_id);

uint64_t stream_generation(const std::string& stream_id);
uint32_t discard_audio(const std::string& stream_id, uint64_t consumer_id, uint32_t frames);
uint32_t consume_audio(const std::string& stream_id, uint64_t consumer_id,
                       float* left, float* right, uint32_t frames, bool silence_on_underrun);

StreamStats stream_stats(const std::string& stream_id);

}  // namespace vivid_cef_audio
