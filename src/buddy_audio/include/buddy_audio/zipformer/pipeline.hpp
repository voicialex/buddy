#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "buddy_audio/infer/backend.hpp"

namespace zipformer {

struct PipelineOptions {
    std::string encoder_model;
    std::string decoder_model;
    std::string joiner_model;
    std::string tokens_path;
    std::string runtime = "onnxruntime";
    int sample_rate = 16000;
    int feature_dim = 80;
    int blank_id = 0;
    int unk_id = 1;
    int context_size = 2;
    int max_symbols_per_frame = 2;
};

class Pipeline {
public:
    Pipeline();
    ~Pipeline();
    Pipeline(Pipeline&&) noexcept;
    Pipeline& operator=(Pipeline&&) noexcept;
    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;

    bool init(const PipelineOptions& options);
    void reset();

    /// Feed audio samples, returns decoded token IDs for this chunk.
    std::vector<int64_t> accept_waveform(const float* samples, int n, bool is_final);

    /// Load tokens file and return token table.
    static std::vector<std::string> load_tokens(const std::string& path);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    PipelineOptions options_;
};

}  // namespace zipformer
