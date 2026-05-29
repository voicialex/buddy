#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct Tensor {
    std::vector<int64_t> shape;
    std::vector<float> data;
};

class IInferenceBackend {
public:
    virtual ~IInferenceBackend() = default;
    virtual std::vector<Tensor> Run(const std::vector<float>& input, const std::vector<int64_t>& shape) = 0;
};

std::unique_ptr<IInferenceBackend> CreateOnnxRuntimeBackend(const std::string& model_path, bool prefer_gpu);

#if HAS_RKNN
std::unique_ptr<IInferenceBackend> CreateRknnBackend(const std::string& model_path);
#endif
