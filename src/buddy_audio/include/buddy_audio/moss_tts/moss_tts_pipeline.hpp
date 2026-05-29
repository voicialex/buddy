#pragma once

#include <memory>
#include <string>
#include <vector>

namespace moss_tts {

struct GenerateParams {
    std::string text;
    std::string prompt_wav_path;
    std::string voice;
    int max_new_frames = 375;
    int seed = 1234;
};

class MossTtsPipeline {
public:
    MossTtsPipeline();
    ~MossTtsPipeline();

    bool init(const std::string& model_dir);
    std::vector<float> generate(const GenerateParams& params);

    bool is_ready() const;
    int sample_rate() const;
    int channels() const;
    double last_inference_seconds() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace moss_tts
