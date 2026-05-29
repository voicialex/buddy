#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace moss_tts {

class Tokenizer {
public:
    Tokenizer();
    ~Tokenizer();

    bool load(const std::string& model_path);
    std::vector<int32_t> encode(const std::string& text) const;
    bool is_loaded() const { return loaded_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    bool loaded_ = false;
};

}  // namespace moss_tts
