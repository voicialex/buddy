#pragma once
#include <memory>
#include <string>
#include <vector>

#include "buddy_audio/infer/tensor.hpp"

namespace infer {

class InferBackend {
public:
    virtual ~InferBackend() = default;
    virtual void load_model(const std::string& model_path) = 0;
    virtual TensorMap run(const TensorMap& inputs, const std::vector<std::string>& output_names) = 0;
    virtual bool is_loaded() const = 0;
};

/// Factory: create backend by runtime name ("onnxruntime" | "rknnruntime")
std::unique_ptr<InferBackend> create_infer_backend(const std::string& runtime);

}  // namespace infer
