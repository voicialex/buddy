#include "buddy_audio/zipformer/pipeline.hpp"

#include "buddy_audio/infer/backend.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace zipformer {

namespace {

constexpr int kEncoderDim = 512;
constexpr int kEncoderChunkSize = 103;
constexpr int kNumEncoderLayers = 5;
constexpr int kCacheHiddenDim = 256;
constexpr int kCacheConvKernel = 30;
constexpr int kNumHeads = 2;
constexpr int kFrameLengthMs = 25;
constexpr int kFrameShiftMs = 10;

const int kCacheKeyLens[kNumEncoderLayers] = {192, 96, 48, 24, 96};

bool starts_with(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

size_t next_pow2(size_t v) {
    size_t n = 1;
    while (n < v) n <<= 1;
    return n;
}

void fft_inplace(std::vector<std::complex<float>>& a) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        while (j & bit) { j ^= bit; bit >>= 1; }
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        float angle = -2.0f * static_cast<float>(M_PI) / static_cast<float>(len);
        std::complex<float> wlen(std::cos(angle), std::sin(angle));
        for (size_t i = 0; i < n; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (size_t j = 0; j < len / 2; ++j) {
                auto u = a[i + j];
                auto v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

}  // namespace

struct Pipeline::Impl {
    std::unique_ptr<infer::InferBackend> encoder;
    std::unique_ptr<infer::InferBackend> decoder;
    std::unique_ptr<infer::InferBackend> joiner;
    infer::TensorMap encoder_state;

    std::vector<int64_t> decoder_context;
    infer::Tensor decoder_out;
    bool decoder_ready = false;

    std::vector<float> sample_buffer;
    size_t next_frame_start = 0;
    std::vector<float> feature_buffer;
    std::vector<int64_t> all_tokens;

    int frame_length = 400;
    int frame_shift = 160;
    int n_fft = 512;
    int spec_bins = 257;
    float preemph = 0.97f;
    float window_power = 0.85f;
    float mel_low_freq = 20.0f;
    float log_floor = 1e-10f;
    std::vector<float> window;
    std::vector<float> mel_filterbank;
};

Pipeline::Pipeline() = default;
Pipeline::~Pipeline() = default;
Pipeline::Pipeline(Pipeline&&) noexcept = default;
Pipeline& Pipeline::operator=(Pipeline&&) noexcept = default;

bool Pipeline::init(const PipelineOptions& options) {
    options_ = options;
    impl_ = std::make_unique<Impl>();

    impl_->encoder = infer::create_infer_backend(options.runtime);
    impl_->decoder = infer::create_infer_backend(options.runtime);
    impl_->joiner = infer::create_infer_backend(options.runtime);

    impl_->encoder->load_model(options.encoder_model);
    impl_->decoder->load_model(options.decoder_model);
    impl_->joiner->load_model(options.joiner_model);

    impl_->frame_length = kFrameLengthMs * options.sample_rate / 1000;
    impl_->frame_shift = kFrameShiftMs * options.sample_rate / 1000;
    impl_->n_fft = static_cast<int>(next_pow2(static_cast<size_t>(impl_->frame_length)));
    impl_->spec_bins = impl_->n_fft / 2 + 1;

    // Povey window
    impl_->window.resize(static_cast<size_t>(impl_->frame_length));
    for (int i = 0; i < impl_->frame_length; ++i) {
        float phase = 2.0f * static_cast<float>(M_PI) * i / (impl_->frame_length - 1);
        float hann = 0.5f - 0.5f * std::cos(phase);
        impl_->window[i] = std::pow(std::max(hann, 0.0f), impl_->window_power);
    }

    // Mel filterbank
    auto hz_to_mel = [](float hz) { return 2595.0f * std::log10(1.0f + hz / 700.0f); };
    auto mel_to_hz = [](float mel) { return 700.0f * (std::pow(10.0f, mel / 2595.0f) - 1.0f); };

    float nyquist = static_cast<float>(options.sample_rate) * 0.5f;
    float high_freq = nyquist - 400.0f;
    high_freq = std::min(high_freq, nyquist - 1.0f);
    high_freq = std::max(high_freq, impl_->mel_low_freq + 10.0f);

    float mel_min = hz_to_mel(impl_->mel_low_freq);
    float mel_max = hz_to_mel(high_freq);
    std::vector<float> hz_points(static_cast<size_t>(options.feature_dim + 2));
    for (int i = 0; i < options.feature_dim + 2; ++i) {
        float mel = mel_min + (mel_max - mel_min) * i / static_cast<float>(options.feature_dim + 1);
        hz_points[i] = mel_to_hz(mel);
    }

    impl_->mel_filterbank.assign(
        static_cast<size_t>(options.feature_dim) * impl_->spec_bins, 0.0f);
    for (int m = 0; m < options.feature_dim; ++m) {
        float left = hz_points[m];
        float center = hz_points[m + 1];
        float right = hz_points[m + 2];
        float left_bin = (impl_->n_fft + 1) * left / options.sample_rate;
        float center_bin = (impl_->n_fft + 1) * center / options.sample_rate;
        float right_bin = (impl_->n_fft + 1) * right / options.sample_rate;
        for (int k = 0; k < impl_->spec_bins; ++k) {
            float fk = static_cast<float>(k);
            float w = 0.0f;
            if (fk >= left_bin && fk <= center_bin && center_bin > left_bin)
                w = (fk - left_bin) / (center_bin - left_bin);
            else if (fk > center_bin && fk <= right_bin && right_bin > center_bin)
                w = (right_bin - fk) / (right_bin - center_bin);
            impl_->mel_filterbank[static_cast<size_t>(m) * impl_->spec_bins + k] = std::max(w, 0.0f);
        }
    }

    reset();
    return true;
}

void Pipeline::reset() {
    if (!impl_) return;

    impl_->encoder_state.clear();
    for (int i = 0; i < kNumEncoderLayers; ++i) {
        // cached_len
        impl_->encoder_state["cached_len_" + std::to_string(i)] =
            infer::Tensor::from_int64(std::vector<int64_t>(kNumHeads, 0), {kNumHeads, 1});
        // cached_avg
        impl_->encoder_state["cached_avg_" + std::to_string(i)] =
            infer::Tensor::from_float(
                std::vector<float>(kNumHeads * kCacheHiddenDim, 0.0f),
                {kNumHeads, 1, kCacheHiddenDim});
        // cached_key, cached_val, cached_val2
        for (const auto& prefix : {"cached_key_", "cached_val_", "cached_val2_"}) {
            int d = kCacheKeyLens[i];
            int last = starts_with(prefix, "cached_key_") ? 192 : 96;
            impl_->encoder_state[std::string(prefix) + std::to_string(i)] =
                infer::Tensor::from_float(
                    std::vector<float>(kNumHeads * d * last, 0.0f),
                    {kNumHeads, d, 1, last});
        }
        // cached_conv1, cached_conv2
        for (const auto& prefix : {"cached_conv1_", "cached_conv2_"}) {
            impl_->encoder_state[std::string(prefix) + std::to_string(i)] =
                infer::Tensor::from_float(
                    std::vector<float>(kNumHeads * kCacheHiddenDim * kCacheConvKernel, 0.0f),
                    {kNumHeads, 1, kCacheHiddenDim, kCacheConvKernel});
        }
    }

    impl_->decoder_context.assign(static_cast<size_t>(options_.context_size), options_.blank_id);
    impl_->decoder_ready = false;
    impl_->sample_buffer.clear();
    impl_->next_frame_start = 0;
    impl_->feature_buffer.clear();
    impl_->all_tokens.clear();
}

std::vector<std::string> Pipeline::load_tokens(const std::string& path) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("Cannot open tokens: " + path);
    std::vector<std::string> tokens;
    std::string line;
    while (std::getline(f, line)) {
        auto sp = line.find(' ');
        if (sp != std::string::npos) {
            tokens.push_back(line.substr(0, sp));
        } else {
            tokens.push_back(line);
        }
    }
    return tokens;
}

// Internal: compute fbank for given frame samples (must be frame_length long)
static std::vector<float> compute_fbank_frame(
    const float* samples, int frame_length, int n_fft, int spec_bins,
    int feature_dim, float preemph, const float* window, const float* mel_fb, float log_floor) {
    std::vector<std::complex<float>> fft_buf(static_cast<size_t>(n_fft), {0.0f, 0.0f});

    // DC removal
    double mean = 0.0;
    for (int i = 0; i < frame_length; ++i) mean += samples[i];
    mean /= frame_length;

    // Preemphasis + window
    std::vector<float> temp(frame_length);
    for (int i = 0; i < frame_length; ++i) temp[i] = samples[i] - static_cast<float>(mean);
    for (int i = frame_length - 1; i >= 1; --i) temp[i] -= preemph * temp[i - 1];
    temp[0] -= preemph * temp[0];
    for (int i = 0; i < frame_length; ++i)
        fft_buf[i] = std::complex<float>(temp[i] * window[i], 0.0f);

    fft_inplace(fft_buf);

    // Power spectrum → mel → log
    std::vector<float> feat(feature_dim);
    for (int m = 0; m < feature_dim; ++m) {
        float energy = 0.0f;
        const float* mw = mel_fb + static_cast<size_t>(m) * spec_bins;
        for (int k = 0; k < spec_bins; ++k)
            energy += std::norm(fft_buf[k]) * mw[k];
        feat[m] = std::log(std::max(energy, log_floor));
    }
    return feat;
}

std::vector<int64_t> Pipeline::accept_waveform(const float* samples, int n, bool is_final) {
    if (!impl_) throw std::runtime_error("Pipeline not initialized");

    if (n > 0) {
        impl_->sample_buffer.insert(impl_->sample_buffer.end(), samples, samples + n);
    }

    // Extract frames
    while (impl_->next_frame_start + impl_->frame_length <= impl_->sample_buffer.size()) {
        auto feat = compute_fbank_frame(
            impl_->sample_buffer.data() + impl_->next_frame_start,
            impl_->frame_length, impl_->n_fft, impl_->spec_bins,
            options_.feature_dim, impl_->preemph,
            impl_->window.data(), impl_->mel_filterbank.data(), impl_->log_floor);
        impl_->feature_buffer.insert(impl_->feature_buffer.end(), feat.begin(), feat.end());
        impl_->next_frame_start += impl_->frame_shift;
    }

    // Final: pad remaining samples to one frame
    if (is_final && impl_->next_frame_start < impl_->sample_buffer.size()) {
        std::vector<float> frame(impl_->frame_length, 0.0f);
        size_t remain = impl_->sample_buffer.size() - impl_->next_frame_start;
        std::copy(impl_->sample_buffer.data() + impl_->next_frame_start,
                  impl_->sample_buffer.data() + impl_->next_frame_start + remain,
                  frame.data());
        auto feat = compute_fbank_frame(
            frame.data(), impl_->frame_length, impl_->n_fft, impl_->spec_bins,
            options_.feature_dim, impl_->preemph,
            impl_->window.data(), impl_->mel_filterbank.data(), impl_->log_floor);
        impl_->feature_buffer.insert(impl_->feature_buffer.end(), feat.begin(), feat.end());
        impl_->next_frame_start += remain;
    }

    // Trim consumed samples
    if (impl_->next_frame_start > static_cast<size_t>(options_.sample_rate)) {
        impl_->sample_buffer.erase(impl_->sample_buffer.begin(),
                                   impl_->sample_buffer.begin() + impl_->next_frame_start);
        impl_->next_frame_start = 0;
    }

    // Process encoder chunks
    std::vector<int64_t> step_tokens;
    int feat_frames = static_cast<int>(impl_->feature_buffer.size() / options_.feature_dim);

    while (feat_frames >= kEncoderChunkSize || (is_final && feat_frames > 0)) {
        int use_frames = std::min(feat_frames, kEncoderChunkSize);

        // Build encoder input
        infer::TensorMap enc_inputs = impl_->encoder_state;
        std::vector<float> x_data(kEncoderChunkSize * options_.feature_dim, 0.0f);
        std::copy(impl_->feature_buffer.begin(),
                  impl_->feature_buffer.begin() + use_frames * options_.feature_dim,
                  x_data.begin());
        enc_inputs["x"] = infer::Tensor::from_float(
            std::move(x_data), {1, kEncoderChunkSize, options_.feature_dim});

        auto enc_outputs = impl_->encoder->run(enc_inputs, {});

        auto out_it = enc_outputs.find("encoder_out");
        if (out_it == enc_outputs.end()) throw std::runtime_error("encoder_out not found");
        const auto& enc_out = out_it->second;
        int out_frames = static_cast<int>(enc_out.shape[1]);

        // Decode frames
        if (!impl_->decoder_ready) {
            infer::TensorMap dec_in;
            dec_in["y"] = infer::Tensor::from_int64(impl_->decoder_context, {1, options_.context_size});
            auto dec_out = impl_->decoder->run(dec_in, {"decoder_out"});
            impl_->decoder_out = std::move(dec_out.at("decoder_out"));
            impl_->decoder_ready = true;
        }

        const float* enc_data = enc_out.ptr<float>();
        for (int t = 0; t < out_frames; ++t) {
            int symbols = 0;
            while (symbols < options_.max_symbols_per_frame) {
                infer::TensorMap joiner_in;
                std::vector<float> enc_frame(enc_data + t * kEncoderDim,
                                             enc_data + (t + 1) * kEncoderDim);
                joiner_in["encoder_out"] = infer::Tensor::from_float(
                    std::move(enc_frame), {1, kEncoderDim});
                joiner_in["decoder_out"] = impl_->decoder_out;

                auto joiner_out = impl_->joiner->run(joiner_in, {"logit"});
                const auto& logit = joiner_out.at("logit");
                const float* lp = logit.ptr<float>();
                size_t vocab = logit.numel();

                int64_t next_id = static_cast<int64_t>(
                    std::distance(lp, std::max_element(lp, lp + vocab)));

                if (next_id == options_.blank_id || next_id == options_.unk_id) break;

                step_tokens.push_back(next_id);
                impl_->all_tokens.push_back(next_id);
                impl_->decoder_context.erase(impl_->decoder_context.begin());
                impl_->decoder_context.push_back(next_id);

                infer::TensorMap dec_in;
                dec_in["y"] = infer::Tensor::from_int64(impl_->decoder_context, {1, options_.context_size});
                auto dec_out = impl_->decoder->run(dec_in, {"decoder_out"});
                impl_->decoder_out = std::move(dec_out.at("decoder_out"));
                ++symbols;
            }
        }

        // Update encoder state
        for (auto& [key, val] : enc_outputs) {
            if (starts_with(key, "new_cached_")) {
                std::string state_name = key.substr(4);  // "new_cached_X" -> "cached_X"
                impl_->encoder_state[state_name] = std::move(val);
            }
        }

        impl_->feature_buffer.erase(
            impl_->feature_buffer.begin(),
            impl_->feature_buffer.begin() + use_frames * options_.feature_dim);
        feat_frames = static_cast<int>(impl_->feature_buffer.size() / options_.feature_dim);
    }

    return step_tokens;
}

}  // namespace zipformer
