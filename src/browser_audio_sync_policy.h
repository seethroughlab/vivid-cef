#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

enum class BrowserAudioSyncAction {
    None,
    Skip,
    Duplicate,
    Silence,
};

enum class BrowserAudioSyncMode {
    Locked,
    CorrectingHigh,
    CorrectingLow,
};

struct BrowserAudioSyncDecision {
    BrowserAudioSyncAction action = BrowserAudioSyncAction::None;
    float drift_ms = 0.0f;
    uint32_t frame_count = 0;
};

inline BrowserAudioSyncDecision decide_browser_audio_sync_stateful_gated(
    float drift_ms,
    float max_drift_ms,
    float sync_strength,
    uint32_t buffer_frames,
    BrowserAudioSyncMode& mode,
    bool gate_active) {
    BrowserAudioSyncDecision d{};
    d.drift_ms = drift_ms;

    const float bounded_max = std::max(1.0f, max_drift_ms);
    const float enter_ms = std::max(bounded_max + 2.0f, bounded_max * 1.15f);
    const float exit_ms = std::max(0.5f, bounded_max * 0.65f);
    const float severe_underrun_ms = std::max(120.0f, bounded_max * 2.25f);
    const float severity = std::clamp(sync_strength, 0.0f, 1.0f);

    if (gate_active || severity <= 0.0f) {
        mode = BrowserAudioSyncMode::Locked;
        return d;
    }

    const float abs_drift = std::fabs(drift_ms);
    if (abs_drift <= exit_ms) {
        mode = BrowserAudioSyncMode::Locked;
        return d;
    }
    if (mode == BrowserAudioSyncMode::Locked && abs_drift < enter_ms) {
        return d;
    }

    if (mode == BrowserAudioSyncMode::Locked) {
        mode = drift_ms >= 0.0f ? BrowserAudioSyncMode::CorrectingHigh
                                : BrowserAudioSyncMode::CorrectingLow;
    } else if (mode == BrowserAudioSyncMode::CorrectingHigh && drift_ms <= -enter_ms) {
        mode = BrowserAudioSyncMode::CorrectingLow;
    } else if (mode == BrowserAudioSyncMode::CorrectingLow && drift_ms >= enter_ms) {
        mode = BrowserAudioSyncMode::CorrectingHigh;
    }

    if (mode == BrowserAudioSyncMode::CorrectingLow || drift_ms <= -enter_ms) {
        if (-drift_ms >= severe_underrun_ms) {
            d.action = BrowserAudioSyncAction::Silence;
            return d;
        }
        const float excess_ms = std::max(0.0f, -drift_ms - bounded_max);
        const uint32_t bounded = std::min<uint32_t>(buffer_frames / 2, 16);
        d.action = BrowserAudioSyncAction::Duplicate;
        d.frame_count = std::min<uint32_t>(
            bounded,
            static_cast<uint32_t>(std::ceil((excess_ms / 1000.0f) * 48000.0f * severity)));
        return d;
    }

    const float excess_ms = std::max(0.0f, drift_ms - bounded_max);
    d.action = BrowserAudioSyncAction::Skip;
    d.frame_count = std::min<uint32_t>(
        16,
        static_cast<uint32_t>(std::ceil((excess_ms / 1000.0f) * 48000.0f * severity)));
    return d;
}
