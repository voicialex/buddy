#include "buddy_audio/io/audio_preprocessor.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#ifdef HAS_WEBRTC_APM
#include <webrtc/modules/audio_processing/include/audio_processing.h>
#endif

namespace {

int clamp_int(int v, int lo, int hi) {
    return std::max(lo, std::min(v, hi));
}

#ifdef HAS_WEBRTC_APM
webrtc::VoiceDetection::Likelihood to_vad_likelihood(int v) {
    switch (clamp_int(v, 0, 3)) {
        case 0:
            return webrtc::VoiceDetection::kVeryLowLikelihood;
        case 1:
            return webrtc::VoiceDetection::kLowLikelihood;
        case 2:
            return webrtc::VoiceDetection::kModerateLikelihood;
        case 3:
        default:
            return webrtc::VoiceDetection::kHighLikelihood;
    }
}

webrtc::NoiseSuppression::Level to_ns_level(int v) {
    switch (clamp_int(v, 0, 3)) {
        case 0:
            return webrtc::NoiseSuppression::kLow;
        case 1:
            return webrtc::NoiseSuppression::kModerate;
        case 2:
            return webrtc::NoiseSuppression::kHigh;
        case 3:
        default:
            return webrtc::NoiseSuppression::kVeryHigh;
    }
}

webrtc::GainControl::Mode to_agc_mode(int v) {
    switch (clamp_int(v, 0, 2)) {
        case 0:
            return webrtc::GainControl::kAdaptiveAnalog;
        case 1:
            return webrtc::GainControl::kAdaptiveDigital;
        case 2:
        default:
            return webrtc::GainControl::kFixedDigital;
    }
}

webrtc::EchoCancellation::SuppressionLevel to_aec_level(int v) {
    switch (clamp_int(v, 0, 2)) {
        case 0:
            return webrtc::EchoCancellation::kLowSuppression;
        case 1:
            return webrtc::EchoCancellation::kModerateSuppression;
        case 2:
        default:
            return webrtc::EchoCancellation::kHighSuppression;
    }
}
#endif

}  // namespace

#ifdef HAS_WEBRTC_APM
namespace {
struct WebrtcState {
    explicit WebrtcState(const AudioPreprocessConfig& cfg, int sample_rate) {
        apm.reset(webrtc::AudioProcessing::Create());
        if (!apm) {
            throw std::runtime_error("AudioProcessing::Create returned null");
        }

        const webrtc::StreamConfig capture_cfg(sample_rate, 1, false);
        const webrtc::StreamConfig render_cfg(sample_rate, 1, false);
        webrtc::ProcessingConfig proc_cfg;
        proc_cfg.input_stream() = capture_cfg;
        proc_cfg.output_stream() = capture_cfg;
        proc_cfg.reverse_input_stream() = render_cfg;
        proc_cfg.reverse_output_stream() = render_cfg;
        if (apm->Initialize(proc_cfg) != webrtc::AudioProcessing::kNoError) {
            throw std::runtime_error("AudioProcessing::Initialize failed");
        }

        if (cfg.vad_enable || cfg.vad_gate_enable) {
            apm->voice_detection()->Enable(true);
            apm->voice_detection()->set_likelihood(to_vad_likelihood(cfg.vad_likelihood));
            apm->voice_detection()->set_frame_size_ms(clamp_int(cfg.vad_frame_ms, 10, 30));
        }

        apm->noise_suppression()->Enable(cfg.ns_enable);
        if (cfg.ns_enable) {
            apm->noise_suppression()->set_level(to_ns_level(cfg.ns_level));
        }

        apm->gain_control()->Enable(cfg.agc_enable);
        if (cfg.agc_enable) {
            apm->gain_control()->set_mode(to_agc_mode(cfg.agc_mode));
            apm->gain_control()->set_target_level_dbfs(clamp_int(cfg.agc_target_level_dbfs, 0, 31));
            apm->gain_control()->set_compression_gain_db(clamp_int(cfg.agc_compression_gain_db, 0, 90));
            apm->gain_control()->enable_limiter(cfg.agc_limiter_enable);
            apm->gain_control()->set_analog_level_limits(
                clamp_int(cfg.agc_analog_level_min, 0, 65535),
                clamp_int(cfg.agc_analog_level_max, 0, 65535));
            apm->gain_control()->set_stream_analog_level(clamp_int(cfg.agc_stream_analog_level, 0, 65535));
        }

        apm->echo_cancellation()->Enable(cfg.aec_enable);
        if (cfg.aec_enable) {
            apm->echo_cancellation()->enable_drift_compensation(false);
            apm->echo_cancellation()->set_suppression_level(to_aec_level(cfg.aec_suppression_level));
            apm->set_stream_delay_ms(std::max(0, cfg.aec_stream_delay_ms));
        }
    }

    std::unique_ptr<webrtc::AudioProcessing> apm;
};
}  // namespace
#endif

AudioPreprocessor::AudioPreprocessor(rclcpp::Logger logger, int sample_rate, const AudioPreprocessConfig& cfg)
    : logger_(logger), sample_rate_(sample_rate), cfg_(cfg) {
    frame_samples_ = std::max(1, sample_rate_ / 100);
    enabled_ = cfg_.enable && (cfg_.vad_enable || cfg_.vad_gate_enable || cfg_.ns_enable || cfg_.agc_enable || cfg_.aec_enable);

    if (!enabled_) {
        return;
    }

#ifdef HAS_WEBRTC_APM
    try {
        auto* state = new WebrtcState(cfg_, sample_rate_);
        webrtc_state_ = state;
        RCLCPP_INFO(
            logger_,
            "Audio preprocessor ready: VAD=%s gate=%s NS=%s AGC=%s AEC=%s",
            cfg_.vad_enable ? "on" : "off",
            cfg_.vad_gate_enable ? "on" : "off",
            cfg_.ns_enable ? "on" : "off",
            cfg_.agc_enable ? "on" : "off",
            cfg_.aec_enable ? "on" : "off");
    } catch (const std::exception& e) {
        enabled_ = false;
        RCLCPP_WARN(logger_, "Audio preprocessor init failed, bypassing: %s", e.what());
    }
#else
    enabled_ = false;
    RCLCPP_WARN(logger_, "WebRTC APM is not linked; preprocessing disabled.");
#endif
}

AudioPreprocessor::~AudioPreprocessor() {
#ifdef HAS_WEBRTC_APM
    auto* state = reinterpret_cast<WebrtcState*>(webrtc_state_);
    delete state;
    webrtc_state_ = nullptr;
#endif
}

AudioPreprocessResult AudioPreprocessor::process_capture_chunk(std::vector<float>* chunk, bool kws_enabled) {
    AudioPreprocessResult result;
    if (!enabled_ || !chunk || chunk->empty()) {
        return result;
    }

#ifdef HAS_WEBRTC_APM
    auto* state = reinterpret_cast<WebrtcState*>(webrtc_state_);
    if (!state || !state->apm) {
        return result;
    }

    int voiced_frames = 0;
    const webrtc::StreamConfig stream_cfg(sample_rate_, 1, false);
    std::vector<float> near_frame(static_cast<size_t>(frame_samples_), 0.0f);
    std::vector<float> reverse_frame(static_cast<size_t>(frame_samples_), 0.0f);

    for (size_t offset = 0; offset < chunk->size(); offset += static_cast<size_t>(frame_samples_)) {
        const size_t avail = std::min(static_cast<size_t>(frame_samples_), chunk->size() - offset);
        std::fill(near_frame.begin(), near_frame.end(), 0.0f);
        std::copy_n(chunk->begin() + static_cast<ptrdiff_t>(offset), avail, near_frame.begin());

        if (cfg_.aec_enable) {
            {
                std::lock_guard<std::mutex> lock(mu_);
                if (render_queue_.size() >= static_cast<size_t>(frame_samples_)) {
                    for (int i = 0; i < frame_samples_; ++i) {
                        reverse_frame[static_cast<size_t>(i)] = render_queue_.front();
                        render_queue_.pop_front();
                    }
                } else {
                    std::fill(reverse_frame.begin(), reverse_frame.end(), 0.0f);
                }
            }
            const float* reverse_src[1] = {reverse_frame.data()};
            float* reverse_dst[1] = {reverse_frame.data()};
            if (state->apm->ProcessReverseStream(reverse_src, stream_cfg, stream_cfg, reverse_dst) !=
                webrtc::AudioProcessing::kNoError) {
                RCLCPP_DEBUG(logger_, "ProcessReverseStream failed");
            }
        }

        const float* near_src[1] = {near_frame.data()};
        float* near_dst[1] = {near_frame.data()};
        if (state->apm->ProcessStream(near_src, stream_cfg, stream_cfg, near_dst) != webrtc::AudioProcessing::kNoError) {
            RCLCPP_DEBUG(logger_, "ProcessStream failed");
        }

        if (cfg_.vad_enable || cfg_.vad_gate_enable) {
            if (state->apm->voice_detection()->stream_has_voice()) {
                ++voiced_frames;
            }
        }

        std::copy_n(near_frame.begin(), avail, chunk->begin() + static_cast<ptrdiff_t>(offset));
    }

    result.has_voice = voiced_frames >= std::max(1, cfg_.vad_min_voice_frames);

    bool gate_enabled = cfg_.vad_gate_enable;
    if (kws_enabled && !cfg_.vad_gate_with_kws) {
        gate_enabled = false;
    }
    if (gate_enabled) {
        if (result.has_voice) {
            vad_hangover_remaining_frames_ = std::max(0, cfg_.vad_hangover_frames);
            result.should_pass = true;
        } else if (vad_hangover_remaining_frames_ > 0) {
            const int frames_used = static_cast<int>(chunk->size() / static_cast<size_t>(frame_samples_));
            vad_hangover_remaining_frames_ = std::max(0, vad_hangover_remaining_frames_ - std::max(1, frames_used));
            result.should_pass = true;
        } else {
            result.should_pass = false;
        }
    }
#else
    (void)kws_enabled;
#endif

    return result;
}

void AudioPreprocessor::resample_and_append_render_locked(const float* samples, int n, int sample_rate) {
    if (!samples || n <= 0) {
        return;
    }
    if (sample_rate <= 0 || sample_rate == sample_rate_) {
        render_queue_.insert(render_queue_.end(), samples, samples + n);
        return;
    }

    const double ratio = static_cast<double>(sample_rate) / static_cast<double>(sample_rate_);
    const int out_n = std::max(1, static_cast<int>(std::lround(static_cast<double>(n) / ratio)));
    for (int i = 0; i < out_n; ++i) {
        const double src_pos = static_cast<double>(i) * ratio;
        const int i0 = clamp_int(static_cast<int>(src_pos), 0, n - 1);
        const int i1 = clamp_int(i0 + 1, 0, n - 1);
        const float frac = static_cast<float>(src_pos - static_cast<double>(i0));
        const float v = samples[i0] * (1.0f - frac) + samples[i1] * frac;
        render_queue_.push_back(v);
    }

    const size_t max_frames = static_cast<size_t>(sample_rate_ * 3);
    if (render_queue_.size() > max_frames) {
        render_queue_.erase(render_queue_.begin(), render_queue_.end() - static_cast<ptrdiff_t>(max_frames));
    }
}

void AudioPreprocessor::push_render_chunk(const float* samples, int n, int sample_rate) {
    if (!enabled_ || !cfg_.aec_enable) {
        return;
    }
    std::lock_guard<std::mutex> lock(mu_);
    resample_and_append_render_locked(samples, n, sample_rate);
}
