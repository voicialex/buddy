#include "buddy_vision/infer_backend.hpp"

#include <cstdint>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <onnxruntime_cxx_api.h>
#include <onnxruntime_c_api.h>

namespace {

float HalfToFloat(uint16_t value) {
    const uint32_t sign = (static_cast<uint32_t>(value & 0x8000)) << 16;
    uint32_t exponent = (value & 0x7C00) >> 10;
    uint32_t mantissa = value & 0x03FF;

    uint32_t bits = 0;
    if (exponent == 0) {
        if (mantissa == 0) {
            bits = sign;
        } else {
            exponent = 1;
            while ((mantissa & 0x0400) == 0) {
                mantissa <<= 1;
                --exponent;
            }
            mantissa &= 0x03FF;
            bits = sign | ((exponent + 112) << 23) | (mantissa << 13);
        }
    } else if (exponent == 0x1F) {
        bits = sign | 0x7F800000 | (mantissa << 13);
    } else {
        bits = sign | ((exponent + 112) << 23) | (mantissa << 13);
    }

    union {
        uint32_t u32;
        float f32;
    } converter{};
    converter.u32 = bits;
    return converter.f32;
}

bool HasCudaProvider() {
    const auto providers = Ort::GetAvailableProviders();
    for (const auto& provider : providers) {
        if (provider == "CUDAExecutionProvider") {
            return true;
        }
    }
    return false;
}

class OnnxRuntimeBackend final : public IInferenceBackend {
public:
    OnnxRuntimeBackend(const std::string& model_path, bool prefer_gpu)
        : env_(ORT_LOGGING_LEVEL_WARNING, "vision_ort"), session_(nullptr) {
        session_options_.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        if (prefer_gpu && HasCudaProvider()) {
            try {
                OrtCUDAProviderOptions cuda_options{};
                cuda_options.device_id = 0;
                session_options_.AppendExecutionProvider_CUDA(cuda_options);
            } catch (const Ort::Exception&) {
                // Fall back to CPU
            }
        }
        session_ = Ort::Session(env_, model_path.c_str(), session_options_);

        Ort::AllocatorWithDefaultOptions allocator;
        const size_t output_count = session_.GetOutputCount();
        output_names_storage_.reserve(output_count);
        output_names_.reserve(output_count);
        for (size_t i = 0; i < output_count; ++i) {
            auto name = session_.GetOutputNameAllocated(i, allocator);
            output_names_storage_.push_back(name.get());
            output_names_.push_back(output_names_storage_.back().c_str());
        }
    }

    std::vector<Tensor> Run(const std::vector<float>& input, const std::vector<int64_t>& shape) override {
        Ort::AllocatorWithDefaultOptions allocator;
        auto input_name = session_.GetInputNameAllocated(0, allocator);
        const char* input_name_cstr = input_name.get();

        Ort::MemoryInfo memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
            memory_info, const_cast<float*>(input.data()), input.size(), shape.data(), shape.size());

        auto outputs = session_.Run(Ort::RunOptions{nullptr}, &input_name_cstr, &input_tensor, 1,
                                    output_names_.data(), output_names_.size());

        std::vector<Tensor> result;
        result.reserve(outputs.size());
        for (auto& output : outputs) {
            const auto info = output.GetTensorTypeAndShapeInfo();
            Tensor tensor;
            tensor.shape = info.GetShape();
            const size_t element_count = info.GetElementCount();
            const auto element_type = info.GetElementType();
            if (element_type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
                const float* data = output.GetTensorData<float>();
                tensor.data.assign(data, data + element_count);
            } else if (element_type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16) {
                const Ort::Float16_t* data = output.GetTensorData<Ort::Float16_t>();
                tensor.data.resize(element_count);
                for (size_t i = 0; i < element_count; ++i) {
                    tensor.data[i] = HalfToFloat(data[i].val);
                }
            } else {
                throw std::runtime_error("Unsupported output tensor type");
            }
            result.push_back(std::move(tensor));
        }
        return result;
    }

private:
    Ort::Env env_;
    Ort::SessionOptions session_options_;
    Ort::Session session_;
    std::vector<std::string> output_names_storage_;
    std::vector<const char*> output_names_;
};

}  // namespace

std::unique_ptr<IInferenceBackend> CreateOnnxRuntimeBackend(const std::string& model_path, bool prefer_gpu) {
    return std::make_unique<OnnxRuntimeBackend>(model_path, prefer_gpu);
}

#if !HAS_RKNN
std::unique_ptr<IInferenceBackend> CreateRknnBackend(const std::string&) {
    throw std::runtime_error("RKNN backend not available in this build");
}
#endif
