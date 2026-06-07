#pragma once

#include "tensor.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace melo_tts {

class InferBackend {
public:
    virtual ~InferBackend() = default;
    virtual void load_model(const std::string& model_path) = 0;
    virtual std::unordered_map<std::string, Tensor> run(
        const std::unordered_map<std::string, Tensor>& inputs,
        const std::vector<std::string>& output_names) = 0;
};

std::unique_ptr<InferBackend> create_backend();

}  // namespace melo_tts
