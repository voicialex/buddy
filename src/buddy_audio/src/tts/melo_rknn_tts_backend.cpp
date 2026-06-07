#include "buddy_audio/tts/tts_backend.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "buddy_audio/melo_rknn/backend.hpp"
#include "buddy_audio/melo_rknn/frontend.hpp"

namespace {

constexpr int64_t kMaxFrames = 1024;
constexpr int64_t kDecoderFrames = 31;
constexpr int64_t kHopSamples = 512;

melo_tts::Tensor align_prior_by_duration(
    const melo_tts::Tensor& phone_prior,
    const melo_tts::Tensor& duration,
    int64_t max_frames,
    int64_t* copied_frames) {
    if (phone_prior.dtype != melo_tts::DType::Float32 || phone_prior.shape.size() != 3 ||
        phone_prior.shape[0] != 1 || phone_prior.shape[1] <= 0 || phone_prior.shape[2] <= 0) {
        throw std::runtime_error("unexpected phone_prior tensor shape");
    }
    if (duration.dtype != melo_tts::DType::Float32 || duration.f32.empty()) {
        throw std::runtime_error("unexpected duration tensor");
    }

    const int64_t phone_len = phone_prior.shape[1];
    const int64_t channels = phone_prior.shape[2];
    melo_tts::Tensor aligned;
    aligned.dtype = melo_tts::DType::Float32;
    aligned.shape = {1, channels, max_frames};
    aligned.f32.assign(static_cast<size_t>(channels * max_frames), 0.0f);

    int64_t out_frame = 0;
    const int64_t duration_count = static_cast<int64_t>(duration.f32.size());
    const int64_t loop_count = std::min(phone_len, duration_count);
    for (int64_t phone = 0; phone < loop_count; ++phone) {
        const float value = duration.f32[static_cast<size_t>(phone)];
        int64_t frames = 0;
        if (std::isfinite(value) && value > 0.0f) {
            frames = std::llround(value);
        }
        for (int64_t repeat = 0; repeat < frames && out_frame < max_frames; ++repeat, ++out_frame) {
            for (int64_t channel = 0; channel < channels; ++channel) {
                aligned.f32[static_cast<size_t>(channel * max_frames + out_frame)] =
                    phone_prior.f32[static_cast<size_t>(phone * channels + channel)];
            }
        }
        if (out_frame >= max_frames) {
            break;
        }
    }
    if (copied_frames != nullptr) {
        *copied_frames = out_frame;
    }
    return aligned;
}

melo_tts::Tensor make_latent_window(
    const melo_tts::Tensor& latent,
    int64_t offset,
    int64_t frame_count,
    int64_t window_frames) {
    if (latent.dtype != melo_tts::DType::Float32 || latent.shape.size() != 3 ||
        latent.shape[0] != 1 || latent.shape[1] <= 0 || latent.shape[2] <= 0) {
        throw std::runtime_error("unexpected latent tensor shape");
    }

    const int64_t channels = latent.shape[1];
    const int64_t total_frames = latent.shape[2];
    melo_tts::Tensor window;
    window.dtype = melo_tts::DType::Float32;
    window.shape = {1, channels, window_frames};
    window.f32.assign(static_cast<size_t>(channels * window_frames), 0.0f);

    const int64_t copy_frames = std::max<int64_t>(
        0, std::min<int64_t>(frame_count, std::min<int64_t>(window_frames, total_frames - offset)));
    for (int64_t c = 0; c < channels; ++c) {
        for (int64_t t = 0; t < copy_frames; ++t) {
            window.f32[static_cast<size_t>(c * window_frames + t)] =
                latent.f32[static_cast<size_t>(c * total_frames + offset + t)];
        }
    }
    return window;
}

std::string resolve_model_root(const TtsBackendConfig& config) {
    if (!config.model_dir.empty() && config.model_dir[0] == '/') {
        return config.model_dir;
    }
    return config.models_dir + "/" + config.model_dir;
}

bool is_split_punct(unsigned char c) {
    switch (c) {
        case '.':
        case ',':
        case ';':
        case ':':
        case '!':
        case '?':
            return true;
        default:
            return false;
    }
}

std::vector<std::string> split_text_fallback(const std::string& text, size_t max_bytes = 80) {
    std::vector<std::string> out;
    if (text.empty()) {
        return out;
    }

    std::string cur;
    cur.reserve(text.size());
    auto flush_cur = [&]() {
        if (!cur.empty()) {
            out.push_back(cur);
            cur.clear();
        }
    };

    for (size_t i = 0; i < text.size();) {
        const unsigned char ch = static_cast<unsigned char>(text[i]);
        const bool is_multi = (ch & 0x80) != 0;

        if (!is_multi && (is_split_punct(ch) || std::isspace(ch) != 0)) {
            if (!cur.empty()) {
                out.push_back(cur);
                cur.clear();
            }
            ++i;
            continue;
        }

        size_t step = 1;
        if (is_multi) {
            if ((ch & 0xE0) == 0xC0) step = 2;
            else if ((ch & 0xF0) == 0xE0) step = 3;
            else if ((ch & 0xF8) == 0xF0) step = 4;
        }
        if (i + step > text.size()) {
            step = 1;
        }

        if (!cur.empty() && cur.size() + step > max_bytes) {
            out.push_back(cur);
            cur.clear();
        }
        cur.append(text, i, step);
        i += step;
    }
    flush_cur();
    return out;
}

class MeloRknnTtsBackend final : public TtsBackend {
public:
    bool initialize(const TtsBackendConfig& config, rclcpp::Logger logger) override {
        logger_ = logger;
        model_root_ = resolve_model_root(config);
        checkpoint_dir_ = model_root_ + "/checkpoint/rknn";
        if (!std::filesystem::exists(checkpoint_dir_)) {
            checkpoint_dir_ = model_root_;  // Backward compatible with old flat layout.
        }

        const auto required = {
            checkpoint_dir_ + "/prior_model.rknn",
            checkpoint_dir_ + "/flow_model.rknn",
            checkpoint_dir_ + "/decoder_frame31.rknn",
            checkpoint_dir_ + "/configuration.json",
            checkpoint_dir_ + "/vocab.txt",
            model_root_ + "/model/MeloTTS-ONNX/melo_onnx/text/opencpop-strict.txt",
            model_root_ + "/third_party_data/pypinyin/pinyin_dict.json",
        };
        for (const auto& path : required) {
            if (!std::filesystem::exists(path)) {
                RCLCPP_ERROR(logger_, "MeloRknnTtsBackend missing file: %s", path.c_str());
                return false;
            }
        }

        melo_tts::FrontendOptions options;
        options.resource_dir = model_root_;
        options.speaker = config.melo_speaker;
        options.bert_model_path = checkpoint_dir_ + "/bert_lml_model.rknn";
        options.speed = config.melo_speed;
        options.sdp_ratio = config.melo_sdp_ratio;
        options.noise_scale = config.melo_noise_scale;
        options.noise_scale_w = config.melo_noise_scale_w;
        options.bert_mode = config.melo_bert_mode;
        options.rank = config.melo_rank;

        frontend_ = std::make_unique<melo_tts::MeloFrontend>(options);
        sample_rate_ = frontend_->sample_rate();
        speed_ = std::max(0.2f, config.melo_speed);

        prior_backend_ = melo_tts::create_backend();
        flow_backend_ = melo_tts::create_backend();
        decoder_backend_ = melo_tts::create_backend();
        prior_backend_->load_model(checkpoint_dir_ + "/prior_model.rknn");
        flow_backend_->load_model(checkpoint_dir_ + "/flow_model.rknn");
        decoder_backend_->load_model(checkpoint_dir_ + "/decoder_frame31.rknn");

        RCLCPP_INFO(
            logger_,
            "MeloRknnTtsBackend ready (model_root=%s, rank=%s, sample_rate=%d)",
            model_root_.c_str(),
            options.rank.c_str(),
            sample_rate_);
        return true;
    }

    TtsResult generate(const std::string& text) override {
        TtsResult result;
        if (!frontend_ || !prior_backend_ || !flow_backend_ || !decoder_backend_ || text.empty()) {
            return result;
        }

        try {
            std::vector<float> audio;
            auto run_segments = [&](const std::vector<melo_tts::FrontendSegment>& segments) {
                for (const auto& segment : segments) {
                auto prior_outputs = prior_backend_->run(
                    segment.tensors, {"phone_prior", "cond", "duration", "frame_mask", "global_cond"});
                const auto& phone_prior = prior_outputs.at("phone_prior");
                const auto& cond = prior_outputs.at("cond");
                const auto& duration = prior_outputs.at("duration");
                const auto& frame_mask = prior_outputs.at("frame_mask");
                const auto& global_cond = prior_outputs.at("global_cond");

                int64_t valid_frames = 0;
                auto aligned_prior = align_prior_by_duration(phone_prior, duration, kMaxFrames, &valid_frames);
                valid_frames = std::clamp<int64_t>(valid_frames, 1, kMaxFrames);

                std::unordered_map<std::string, melo_tts::Tensor> flow_inputs;
                flow_inputs["/Add_1_output_0"] = std::move(aligned_prior);
                flow_inputs["/Cast_2_output_0"] = frame_mask;
                flow_inputs["/enc_p/encoder/Transpose_output_0"] = global_cond;
                auto flow_outputs = flow_backend_->run(flow_inputs, {"latent"});
                const auto& latent = flow_outputs.at("latent");

                for (int64_t offset = 0; offset < valid_frames; offset += kDecoderFrames) {
                    const int64_t chunk_frames = std::min<int64_t>(kDecoderFrames, valid_frames - offset);
                    std::unordered_map<std::string, melo_tts::Tensor> decoder_inputs;
                    decoder_inputs["/Mul_9_output_0"] =
                        make_latent_window(latent, offset, chunk_frames, kDecoderFrames);
                    decoder_inputs["/dec/cond/Conv_output_0"] = cond;
                    auto decoder_outputs = decoder_backend_->run(decoder_inputs, {"audio"});
                    const auto& chunk_audio = decoder_outputs.at("audio").f32;
                    const size_t wanted_samples = static_cast<size_t>(chunk_frames * kHopSamples);
                    const size_t copy_samples = std::min(wanted_samples, chunk_audio.size());
                    audio.insert(
                        audio.end(),
                        chunk_audio.begin(),
                        chunk_audio.begin() + static_cast<std::ptrdiff_t>(copy_samples));
                }
                audio.insert(audio.end(), static_cast<size_t>((sample_rate_ * 0.05f) / speed_), 0.0f);
                }
            };

            try {
                run_segments(frontend_->prepare(text));
            } catch (const std::exception& e) {
                const std::string err = e.what();
                const bool is_static_shape =
                    err.find("static RKNN shape") != std::string::npos ||
                    err.find("static BERT shape") != std::string::npos;
                if (!is_static_shape) {
                    throw;
                }
                RCLCPP_WARN(logger_,
                            "MeloRknnTtsBackend long segment detected, fallback split enabled: %s",
                            err.c_str());
                const auto pieces = split_text_fallback(text);
                if (pieces.size() <= 1) {
                    throw;
                }
                for (const auto& piece : pieces) {
                    if (piece.empty()) {
                        continue;
                    }
                    run_segments(frontend_->prepare(piece));
                }
            }
            result.samples = std::move(audio);
            result.sample_rate = sample_rate_;
        } catch (const std::exception& e) {
            RCLCPP_ERROR(logger_, "MeloRknnTtsBackend generate failed: %s", e.what());
        }
        return result;
    }

private:
    rclcpp::Logger logger_{rclcpp::get_logger("melo_rknn_tts_backend")};
    std::string model_root_;
    std::string checkpoint_dir_;
    std::unique_ptr<melo_tts::MeloFrontend> frontend_;
    std::unique_ptr<melo_tts::InferBackend> prior_backend_;
    std::unique_ptr<melo_tts::InferBackend> flow_backend_;
    std::unique_ptr<melo_tts::InferBackend> decoder_backend_;
    int sample_rate_ = 44100;
    float speed_ = 1.0f;
};

}  // namespace

std::unique_ptr<TtsBackend> create_melo_rknn_tts_backend() {
    return std::make_unique<MeloRknnTtsBackend>();
}
