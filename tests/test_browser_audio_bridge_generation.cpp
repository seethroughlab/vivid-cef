#include "browser_audio_bridge.h"

#include <cstdio>
#include <cstdint>
#include <cstring>

using namespace vivid_cef_audio;

static int g_fail = 0;

static void check(bool cond, const char* msg) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        ++g_fail;
    }
}

int main() {
    const std::string stream = "test_stream_generation";
    const uint64_t consumer = 0xCAFEULL;

    check(claim_consumer(stream, consumer), "consumer claims stream");

    producer_stream_started(stream, 48000, 1);
    producer_source_reset(stream);
    const uint64_t g1 = stream_generation(stream);

    const float mono1[4] = {0.1f, 0.2f, 0.3f, 0.4f};
    const float* p1[1] = {mono1};
    producer_push_packet(stream, p1, 4, 1, 48000, 0);

    float l[4]{};
    float r[4]{};
    uint32_t got = consume_audio(stream, consumer, l, r, 4, true);
    check(got == 4, "consume reads frames before reset");

    producer_source_reset(stream);
    const uint64_t g2 = stream_generation(stream);
    check(g2 > g1, "source reset bumps generation");

    std::memset(l, 0x7F, sizeof(l));
    std::memset(r, 0x7F, sizeof(r));
    got = consume_audio(stream, consumer, l, r, 4, true);
    check(got == 0, "consume returns no stale frames after reset");
    check(l[0] == 0.0f && r[0] == 0.0f, "stale samples are flushed after reset");

    const float mono2[4] = {0.9f, 0.8f, 0.7f, 0.6f};
    const float* p2[1] = {mono2};
    producer_push_packet(stream, p2, 4, 1, 48000, 0);
    got = consume_audio(stream, consumer, l, r, 4, true);
    check(got == 4, "consume reads frames after reset");
    check(l[0] == 0.9f && r[0] == 0.9f, "new generation samples are emitted");

    release_consumer(stream, consumer);
    producer_stream_stopped(stream);
    return g_fail == 0 ? 0 : 1;
}
