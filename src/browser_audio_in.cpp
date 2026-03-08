#include "browser_audio_bridge.h"
#include "operator_api/audio_operator.h"
#include "operator_api/operator.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

struct BrowserAudioIn : vivid::OperatorBase {
    static constexpr const char* kName   = "BrowserAudioIn";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_AUDIO;
    static constexpr bool kTimeDependent = true;

    vivid::Param<vivid::TextValue> stream_id      {"stream_id"};
    vivid::Param<float>            gain           {"gain", 1.0f, 0.0f, 2.0f};
    vivid::Param<float>            sync_strength  {"sync_strength", 1.0f, 0.0f, 1.0f};
    vivid::Param<float>            max_drift_ms   {"max_drift_ms", 80.0f, 5.0f, 500.0f};
    vivid::Param<int>              dropout_behavior{"dropout_behavior", 0, {"Silence"}};

    BrowserAudioIn() {
        vivid::semantic_tag(stream_id, "id_stream");
        vivid::semantic_shape(stream_id, "string");

        vivid::semantic_tag(gain, "amplitude_linear");
        vivid::semantic_shape(gain, "scalar");

        vivid::semantic_tag(sync_strength, "x_sync_strength");
        vivid::semantic_shape(sync_strength, "scalar");

        vivid::semantic_tag(max_drift_ms, "time_milliseconds");
        vivid::semantic_shape(max_drift_ms, "scalar");
        vivid::semantic_unit(max_drift_ms, "ms");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&stream_id);
        out.push_back(&gain);
        out.push_back(&sync_strength);
        out.push_back(&max_drift_ms);
        out.push_back(&dropout_behavior);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"left", VIVID_PORT_AUDIO_FLOAT, VIVID_PORT_OUTPUT});
        out.push_back({"right", VIVID_PORT_AUDIO_FLOAT, VIVID_PORT_OUTPUT});
    }

    void process(const VividProcessContext* ctx) override {
        auto* audio = vivid_audio(ctx);
        if (!audio) return;

        float* left = audio->output_buffers[0];
        float* right = audio->output_buffers[1];
        uint32_t n = audio->buffer_size;

        const std::string sid = stream_id.str_value;
        if (sid != claimed_stream_id_) {
            if (!claimed_stream_id_.empty()) {
                vivid_cef_audio::release_consumer(claimed_stream_id_, consumer_id_);
                claimed_stream_id_.clear();
            }

            if (!sid.empty() && vivid_cef_audio::claim_consumer(sid, consumer_id_)) {
                claimed_stream_id_ = sid;
                warned_claim_conflict_ = false;
            } else if (!sid.empty() && !warned_claim_conflict_) {
                warned_claim_conflict_ = true;
                std::fprintf(stderr,
                    "[vivid-cef] BrowserAudioIn: stream '%s' already has a consumer; outputting silence\n",
                    sid.c_str());
            }
        }

        if (claimed_stream_id_.empty()) {
            std::memset(left, 0, n * sizeof(float));
            std::memset(right, 0, n * sizeof(float));
            return;
        }

        bool silence_on_underrun = (dropout_behavior.int_value() == 0);
        vivid_cef_audio::consume_audio(claimed_stream_id_, consumer_id_, left, right, n,
                                       sync_strength.value, max_drift_ms.value,
                                       silence_on_underrun);

        float g = gain.value;
        for (uint32_t i = 0; i < n; ++i) {
            left[i] *= g;
            right[i] *= g;
        }
    }

    ~BrowserAudioIn() override {
        if (!claimed_stream_id_.empty()) {
            vivid_cef_audio::release_consumer(claimed_stream_id_, consumer_id_);
        }
    }

private:
    const uint64_t consumer_id_ = reinterpret_cast<uint64_t>(this);
    std::string claimed_stream_id_;
    bool warned_claim_conflict_ = false;
};

VIVID_REGISTER(BrowserAudioIn)
