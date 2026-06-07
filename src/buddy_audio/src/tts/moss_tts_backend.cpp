#include <algorithm>
#include <cmath>
#include <filesystem>
#include <rclcpp/rclcpp.hpp>

#include "buddy_audio/moss_tts/moss_tts_pipeline.hpp"
#include "buddy_audio/tts/tts_backend.hpp"

namespace {

std::vector<float> linear_resample(const std::vector<float>& input, int src_rate, int dst_rate) {
    if (src_rate == dst_rate || input.empty()) {
        return input;
    }
    const double scale = static_cast<double>(dst_rate) / static_cast<double>(src_rate);
    const size_t output_size = static_cast<size_t>(std::max<int64_t>(1, std::llround(input.size() * scale)));
    std::vector<float> output(output_size, 0.0f);
    for (size_t i = 0; i < output_size; ++i) {
        const double src_pos = static_cast<double>(i) / scale;
        const size_t left = static_cast<size_t>(std::floor(src_pos));
        const size_t right = std::min(left + 1, input.size() - 1);
        const double frac = src_pos - static_cast<double>(left);
        output[i] = static_cast<float>((1.0 - frac) * input[left] + frac * input[right]);
    }
    return output;
}

// Convert interleaved stereo to mono by averaging channels
std::vector<float> stereo_to_mono(const std::vector<float>& interleaved, int channels) {
    if (channels <= 1) {
        return interleaved;
    }
    const size_t frames = interleaved.size() / static_cast<size_t>(channels);
    std::vector<float> mono(frames);
    for (size_t i = 0; i < frames; ++i) {
        float sum = 0.0f;
        for (int ch = 0; ch < channels; ++ch) {
            sum += interleaved[i * static_cast<size_t>(channels) + static_cast<size_t>(ch)];
        }
        mono[i] = sum / static_cast<float>(channels);
    }
    return mono;
}

}  // namespace

class MossTtsBackend : public TtsBackend {
public:
    bool initialize(const TtsBackendConfig& config, rclcpp::Logger logger) override {
        voice_ = config.voice;
        max_new_frames_ = config.max_new_frames;
        seed_ = config.seed;

        std::string full_path;
        if (!config.model_dir.empty() && config.model_dir[0] == '/') {
            full_path = config.model_dir;
        } else {
            full_path = config.models_dir + "/" + config.model_dir;
        }

        if (!std::filesystem::exists(full_path)) {
            RCLCPP_ERROR(logger,
                         "MossTtsBackend: model_dir not found: %s\n"
                         "       Run: ./scripts/setup_prebuilt.sh moss-tts",
                         full_path.c_str());
            return false;
        }

        pipeline_ = std::make_unique<moss_tts::MossTtsPipeline>();
        if (!pipeline_->init(full_path)) {
            RCLCPP_ERROR(logger, "MossTtsBackend: failed to init pipeline at %s", full_path.c_str());
            return false;
        }

        RCLCPP_INFO(logger,
                    "MossTtsBackend ready (model_dir=%s, sample_rate=%d, channels=%d)",
                    full_path.c_str(), pipeline_->sample_rate(), pipeline_->channels());
        return true;
    }

    TtsResult generate(const std::string& text) override {
        TtsResult result;
        if (!pipeline_ || !pipeline_->is_ready()) {
            return result;
        }

        moss_tts::GenerateParams params;
        params.text = text;
        params.voice = voice_;
        params.max_new_frames = max_new_frames_;
        params.seed = seed_;

        std::vector<float> audio = pipeline_->generate(params);
        if (audio.empty()) {
            return result;
        }

        // Convert to mono
        std::vector<float> mono = stereo_to_mono(audio, pipeline_->channels());

        // Resample to 16kHz (pipeline outputs 48kHz)
        constexpr int kTargetSampleRate = 16000;
        std::vector<float> resampled = linear_resample(mono, pipeline_->sample_rate(), kTargetSampleRate);

        result.samples = std::move(resampled);
        result.sample_rate = kTargetSampleRate;
        return result;
    }

private:
    std::unique_ptr<moss_tts::MossTtsPipeline> pipeline_;
    std::string voice_;
    int max_new_frames_ = 375;
    int seed_ = 1234;
};

std::unique_ptr<TtsBackend> create_moss_tts_backend() {
    return std::make_unique<MossTtsBackend>();
}
