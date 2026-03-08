#include "browser_audio_bridge.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace vivid_cef_audio {
namespace {

struct StreamState {
    static constexpr uint32_t kCapacityFrames = kTargetSampleRate * 4;  // ~4s

    std::vector<float> left;
    std::vector<float> right;

    std::atomic<uint64_t> write_pos{0};
    std::atomic<uint64_t> read_pos{0};

    std::atomic<uint64_t> underruns{0};
    std::atomic<uint64_t> overruns{0};

    std::atomic<bool> producer_active{false};
    std::atomic<uint32_t> producer_sample_rate{0};
    std::atomic<int> producer_channels{0};
    std::atomic<int64_t> last_pts_ms{0};

    std::atomic<uint64_t> consumer_owner{0};

    // Producer-side scratch (single producer thread, mutex-guarded)
    std::mutex producer_mutex;
    std::vector<float> scratch_l;
    std::vector<float> scratch_r;
    std::vector<float> resampled_l;
    std::vector<float> resampled_r;

    // Consumer-side state (single consumer, audio thread)
    float last_l = 0.0f;
    float last_r = 0.0f;

    StreamState() : left(kCapacityFrames, 0.0f), right(kCapacityFrames, 0.0f) {}
};

std::mutex g_registry_mutex;
std::unordered_map<std::string, std::shared_ptr<StreamState>> g_streams;

std::shared_ptr<StreamState> get_or_create_stream(const std::string& stream_id) {
    if (stream_id.empty()) return nullptr;
    std::lock_guard<std::mutex> lock(g_registry_mutex);
    auto it = g_streams.find(stream_id);
    if (it != g_streams.end()) return it->second;

    auto state = std::make_shared<StreamState>();
    g_streams.emplace(stream_id, state);
    return state;
}

std::shared_ptr<StreamState> find_stream(const std::string& stream_id) {
    if (stream_id.empty()) return nullptr;
    std::lock_guard<std::mutex> lock(g_registry_mutex);
    auto it = g_streams.find(stream_id);
    if (it == g_streams.end()) return nullptr;
    return it->second;
}

inline float clampf(float x, float lo, float hi) {
    return std::max(lo, std::min(hi, x));
}

}  // namespace

void producer_stream_started(const std::string& stream_id, int sample_rate, int channels) {
    auto stream = get_or_create_stream(stream_id);
    if (!stream) return;

    stream->producer_sample_rate.store(sample_rate > 0 ? static_cast<uint32_t>(sample_rate) : 0,
                                       std::memory_order_relaxed);
    stream->producer_channels.store(channels, std::memory_order_relaxed);
    stream->producer_active.store(true, std::memory_order_release);
}

void producer_stream_stopped(const std::string& stream_id) {
    auto stream = find_stream(stream_id);
    if (!stream) return;
    stream->producer_active.store(false, std::memory_order_release);
}

void producer_stream_error(const std::string& stream_id, const std::string& message) {
    (void)message;
    auto stream = find_stream(stream_id);
    if (!stream) return;
    stream->producer_active.store(false, std::memory_order_release);
}

void producer_push_packet(const std::string& stream_id, const float** data,
                          int frames, int channels, int sample_rate, int64_t pts_ms) {
    auto stream = get_or_create_stream(stream_id);
    if (!stream || !data || frames <= 0 || channels <= 0 || sample_rate <= 0) return;

    std::lock_guard<std::mutex> lock(stream->producer_mutex);

    stream->producer_active.store(true, std::memory_order_release);
    stream->producer_sample_rate.store(static_cast<uint32_t>(sample_rate), std::memory_order_relaxed);
    stream->producer_channels.store(channels, std::memory_order_relaxed);
    stream->last_pts_ms.store(pts_ms, std::memory_order_relaxed);

    stream->scratch_l.resize(static_cast<size_t>(frames));
    stream->scratch_r.resize(static_cast<size_t>(frames));

    for (int i = 0; i < frames; ++i) {
        float mono = 0.0f;
        for (int c = 0; c < channels; ++c) mono += data[c][i];
        mono /= static_cast<float>(channels);

        if (channels == 1) {
            stream->scratch_l[i] = mono;
            stream->scratch_r[i] = mono;
        } else {
            float l0 = data[0][i];
            float r0 = data[1][i];
            // Stereo-preserving downmix that still folds additional channels.
            stream->scratch_l[i] = 0.75f * l0 + 0.25f * mono;
            stream->scratch_r[i] = 0.75f * r0 + 0.25f * mono;
        }
    }

    const float* src_l = stream->scratch_l.data();
    const float* src_r = stream->scratch_r.data();
    int src_frames = frames;

    if (sample_rate != static_cast<int>(kTargetSampleRate)) {
        double ratio = static_cast<double>(kTargetSampleRate) / static_cast<double>(sample_rate);
        int out_frames = std::max(1, static_cast<int>(std::llround(static_cast<double>(frames) * ratio)));
        stream->resampled_l.resize(static_cast<size_t>(out_frames));
        stream->resampled_r.resize(static_cast<size_t>(out_frames));

        for (int o = 0; o < out_frames; ++o) {
            double src_pos = static_cast<double>(o) / ratio;
            int i0 = static_cast<int>(std::floor(src_pos));
            int i1 = std::min(i0 + 1, frames - 1);
            float t = static_cast<float>(src_pos - static_cast<double>(i0));
            i0 = std::max(0, std::min(i0, frames - 1));

            stream->resampled_l[o] = src_l[i0] + (src_l[i1] - src_l[i0]) * t;
            stream->resampled_r[o] = src_r[i0] + (src_r[i1] - src_r[i0]) * t;
        }

        src_l = stream->resampled_l.data();
        src_r = stream->resampled_r.data();
        src_frames = out_frames;
    }

    uint64_t write = stream->write_pos.load(std::memory_order_relaxed);
    uint64_t read = stream->read_pos.load(std::memory_order_acquire);
    uint64_t used = write - read;
    uint64_t free = StreamState::kCapacityFrames - 1 - std::min<uint64_t>(used, StreamState::kCapacityFrames - 1);
    if (free == 0) {
        stream->overruns.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    uint32_t to_write = static_cast<uint32_t>(std::min<uint64_t>(src_frames, free));
    if (to_write < static_cast<uint32_t>(src_frames)) {
        stream->overruns.fetch_add(1, std::memory_order_relaxed);
    }

    for (uint32_t i = 0; i < to_write; ++i) {
        uint32_t idx = static_cast<uint32_t>((write + i) % StreamState::kCapacityFrames);
        stream->left[idx] = src_l[i];
        stream->right[idx] = src_r[i];
    }

    stream->write_pos.store(write + to_write, std::memory_order_release);
}

bool claim_consumer(const std::string& stream_id, uint64_t consumer_id) {
    if (stream_id.empty() || consumer_id == 0) return false;
    auto stream = get_or_create_stream(stream_id);
    if (!stream) return false;

    uint64_t expected = 0;
    if (stream->consumer_owner.compare_exchange_strong(expected, consumer_id,
                                                       std::memory_order_acq_rel)) {
        return true;
    }
    return expected == consumer_id;
}

void release_consumer(const std::string& stream_id, uint64_t consumer_id) {
    if (stream_id.empty() || consumer_id == 0) return;
    auto stream = find_stream(stream_id);
    if (!stream) return;

    uint64_t expected = consumer_id;
    stream->consumer_owner.compare_exchange_strong(expected, 0,
                                                   std::memory_order_acq_rel);
}

uint32_t consume_audio(const std::string& stream_id, uint64_t consumer_id,
                       float* left, float* right, uint32_t frames,
                       float sync_strength, float max_drift_ms,
                       bool silence_on_underrun) {
    if (!left || !right || frames == 0) return 0;

    if (stream_id.empty() || consumer_id == 0) {
        std::memset(left, 0, frames * sizeof(float));
        std::memset(right, 0, frames * sizeof(float));
        return 0;
    }

    auto stream = find_stream(stream_id);
    if (!stream) {
        std::memset(left, 0, frames * sizeof(float));
        std::memset(right, 0, frames * sizeof(float));
        return 0;
    }

    if (stream->consumer_owner.load(std::memory_order_acquire) != consumer_id) {
        std::memset(left, 0, frames * sizeof(float));
        std::memset(right, 0, frames * sizeof(float));
        return 0;
    }

    sync_strength = clampf(sync_strength, 0.0f, 1.0f);
    max_drift_ms = std::max(1.0f, max_drift_ms);

    uint64_t read = stream->read_pos.load(std::memory_order_relaxed);
    uint64_t write = stream->write_pos.load(std::memory_order_acquire);
    uint64_t avail = write - read;

    const uint32_t target_frames = kTargetSampleRate / 4;  // 250ms target fill
    float drift_frames = static_cast<float>(static_cast<int64_t>(avail) - static_cast<int64_t>(target_frames));
    float drift_ms = drift_frames * 1000.0f / static_cast<float>(kTargetSampleRate);

    // If buffer is too full, drop a small bounded amount to pull A/V closer.
    if (sync_strength > 0.0f && drift_ms > max_drift_ms) {
        float excess_frames = (drift_ms - max_drift_ms) * static_cast<float>(kTargetSampleRate) / 1000.0f;
        uint32_t correction = static_cast<uint32_t>(std::min(16.0f, excess_frames * sync_strength));
        correction = std::min<uint32_t>(correction, static_cast<uint32_t>(avail));
        read += correction;
        stream->read_pos.store(read, std::memory_order_release);
        avail -= correction;
    }

    uint32_t duplicate_count = 0;
    if (sync_strength > 0.0f && drift_ms < -max_drift_ms) {
        float lacking_frames = (-max_drift_ms - drift_ms) * static_cast<float>(kTargetSampleRate) / 1000.0f;
        duplicate_count = static_cast<uint32_t>(std::min(16.0f, lacking_frames * sync_strength));
        duplicate_count = std::min<uint32_t>(duplicate_count, frames / 2);
    }

    uint32_t wanted = frames - duplicate_count;
    uint32_t to_read = static_cast<uint32_t>(std::min<uint64_t>(wanted, avail));

    for (uint32_t i = 0; i < to_read; ++i) {
        uint32_t idx = static_cast<uint32_t>((read + i) % StreamState::kCapacityFrames);
        left[i] = stream->left[idx];
        right[i] = stream->right[idx];
    }

    if (to_read > 0) {
        stream->last_l = left[to_read - 1];
        stream->last_r = right[to_read - 1];
    }

    stream->read_pos.store(read + to_read, std::memory_order_release);

    // Duplicate the last sample (stretch) if we're under target fill.
    for (uint32_t i = to_read; i < to_read + duplicate_count && i < frames; ++i) {
        left[i] = stream->last_l;
        right[i] = stream->last_r;
    }

    uint32_t produced = to_read + duplicate_count;

    if (produced < frames) {
        if (silence_on_underrun) {
            std::memset(left + produced, 0, (frames - produced) * sizeof(float));
            std::memset(right + produced, 0, (frames - produced) * sizeof(float));
        } else {
            for (uint32_t i = produced; i < frames; ++i) {
                left[i] = stream->last_l;
                right[i] = stream->last_r;
            }
        }
        stream->underruns.fetch_add(1, std::memory_order_relaxed);
    }

    return to_read;
}

StreamStats stream_stats(const std::string& stream_id) {
    StreamStats out{};
    auto stream = find_stream(stream_id);
    if (!stream) return out;

    uint64_t read = stream->read_pos.load(std::memory_order_relaxed);
    uint64_t write = stream->write_pos.load(std::memory_order_acquire);
    uint64_t avail = write - read;

    out.fill_ratio = static_cast<float>(
        std::min<uint64_t>(avail, StreamState::kCapacityFrames - 1)) /
        static_cast<float>(StreamState::kCapacityFrames - 1);

    const int64_t target = static_cast<int64_t>(kTargetSampleRate / 4);
    int64_t drift_frames = static_cast<int64_t>(avail) - target;
    out.drift_ms = static_cast<float>(drift_frames) * 1000.0f /
                   static_cast<float>(kTargetSampleRate);

    out.underruns = stream->underruns.load(std::memory_order_relaxed);
    out.overruns = stream->overruns.load(std::memory_order_relaxed);
    out.producer_active = stream->producer_active.load(std::memory_order_relaxed);
    out.producer_sample_rate = stream->producer_sample_rate.load(std::memory_order_relaxed);
    return out;
}

}  // namespace vivid_cef_audio
