#include "browser_audio_bridge.h"

#include <cmath>
#include <cstdio>
#include <cstdint>

using namespace vivid_cef_audio;

static int g_failures = 0;

static void check(bool cond, const char* msg) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        ++g_failures;
    }
}

static bool approx(float a, float b) {
    return std::fabs(a - b) < 1e-6f;
}

int main() {
    const std::string stream = "test_stream_bridge";
    const uint64_t consumer_id = 0x1234ULL;

    check(claim_consumer(stream, consumer_id), "claim consumer succeeds");

    float l[8]{};
    float r[8]{};
    uint32_t got = consume_audio(stream, consumer_id, l, r, 8, 0.0f, 80.0f, true);
    check(got == 0, "consume returns 0 when no producer data");
    bool all_zero = true;
    for (int i = 0; i < 8; ++i) {
        all_zero = all_zero && approx(l[i], 0.0f) && approx(r[i], 0.0f);
    }
    check(all_zero, "underrun without producer outputs silence");

    producer_stream_started(stream, 48000, 1);
    const float mono[4] = {0.1f, 0.2f, 0.3f, 0.4f};
    const float* packet[1] = {mono};
    producer_push_packet(stream, packet, 4, 1, 48000, 0);

    float l2[4]{};
    float r2[4]{};
    got = consume_audio(stream, consumer_id, l2, r2, 4, 0.0f, 80.0f, true);
    check(got == 4, "consume reads pushed frames");
    check(approx(l2[0], 0.1f) && approx(r2[0], 0.1f), "mono duplicated to stereo sample 0");
    check(approx(l2[3], 0.4f) && approx(r2[3], 0.4f), "mono duplicated to stereo sample 3");

    release_consumer(stream, consumer_id);
    float l3[4]{1, 1, 1, 1};
    float r3[4]{1, 1, 1, 1};
    got = consume_audio(stream, consumer_id, l3, r3, 4, 0.0f, 80.0f, true);
    check(got == 0, "consume returns 0 after release");
    all_zero = true;
    for (int i = 0; i < 4; ++i) {
        all_zero = all_zero && approx(l3[i], 0.0f) && approx(r3[i], 0.0f);
    }
    check(all_zero, "consume after release outputs silence");

    check(claim_consumer(stream, 0x5678ULL), "second consumer can claim after release");
    producer_stream_stopped(stream);

    if (g_failures == 0) {
        std::fprintf(stderr, "PASS: test_browser_audio_bridge\n");
    }
    return g_failures == 0 ? 0 : 1;
}
