#include "browser_audio_sync_policy.h"

#include <cstdio>

static int g_fail = 0;

static void check(bool cond, const char* msg) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        ++g_fail;
    }
}

int main() {
    BrowserAudioSyncMode mode = BrowserAudioSyncMode::Locked;

    {
        auto d = decide_browser_audio_sync_stateful_gated(10.0f, 20.0f, 1.0f, 256, mode, false);
        check(d.action == BrowserAudioSyncAction::None, "drift inside lock band does not correct");
    }

    {
        auto d = decide_browser_audio_sync_stateful_gated(60.0f, 20.0f, 1.0f, 256, mode, false);
        check(d.action == BrowserAudioSyncAction::Skip, "high positive drift triggers skip");
        check(d.frame_count > 0, "skip frame count positive");
    }

    {
        auto d = decide_browser_audio_sync_stateful_gated(-60.0f, 20.0f, 1.0f, 256, mode, false);
        check(d.action == BrowserAudioSyncAction::Duplicate, "negative drift triggers duplicate");
        check(d.frame_count > 0, "duplicate frame count positive");
    }

    {
        auto d = decide_browser_audio_sync_stateful_gated(-180.0f, 20.0f, 1.0f, 256, mode, false);
        check(d.action == BrowserAudioSyncAction::Silence, "severe underrun triggers silence");
    }

    {
        mode = BrowserAudioSyncMode::CorrectingHigh;
        auto d = decide_browser_audio_sync_stateful_gated(80.0f, 20.0f, 1.0f, 256, mode, true);
        check(d.action == BrowserAudioSyncAction::None, "startup gate suppresses correction");
        check(mode == BrowserAudioSyncMode::Locked, "startup gate resets mode");
    }

    return g_fail == 0 ? 0 : 1;
}
