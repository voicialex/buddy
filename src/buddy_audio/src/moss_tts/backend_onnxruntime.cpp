#include "buddy_audio/moss_tts/backend.hpp"

#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <iostream>
#include <numeric>
#include <stdexcept>

namespace moss_tts {

namespace {

size_t tensor_numel(const std::vector<int64_t>& shape) {
    return static_cast<size_t>(std::accumulate(shape.begin(), shape.end(), int64_t(1), std::multiplies<int64_t>()));
}

}  // namespace

class OnnxRuntimeBackend : public InferBackend {
public:
    OnnxRuntimeBackend()
        : env_(ORT_LOGGING_LEVEL_WARNING, "moss_tts") {
        session_options_.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        session_options_.SetIntraOpNumThreads(4);
        session_options_.SetInterOpNumThreads(1);

#ifdef MOSS_TTS_ENABLE_CUDA
        try {
            OrtCUDAProviderOptions cuda_options{};
            cuda_options.device_id = 0;
            session_options_.AppendExecutionProvider_CUDA(cuda_options);
            use_cuda_ = true;
        } catch (...) {
            use_cuda_ = false;
        }
#endif
    }

    void load_model(const std::string& model_path) override {
        session_ = std::make_unique<Ort::Session>(env_, model_path.c_str(), session_options_);
        loaded_ = true;

        Ort::AllocatorWithDefaultOptions allocator;
        input_names_.clear();
        output_names_.clear();
        for (size_t index = 0; index < session_->GetInputCount(); ++index) {
            auto name = session_->GetInputNameAllocated(index, allocator);
            input_names_.push_back(std::string(name.get()));
        }
        for (size_t index = 0; index < session_->GetOutputCount(); ++index) {
            auto name = session_->GetOutputNameAllocated(index, allocator);
            output_names_.push_back(std::string(name.get()));
        }
    }

    std::unordered_map<std::string, Tensor> run(
        const std::unordered_map<std::string, Tensor>& inputs,
        const std::vector<std::string>& requested_output_names) override {
        if (!loaded_ || session_ == nullptr) {
            throw std::runtime_error("Model session is not loaded.");
        }

        Ort::MemoryInfo memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        std::vector<const char*> input_name_ptrs;
        std::vector<Ort::Value> input_tensors;
        input_name_ptrs.reserve(input_names_.size());
        input_tensors.reserve(input_names_.size());

        for (const std::string& input_name : input_names_) {
            const auto found = inputs.find(input_name);
            if (found == inputs.end()) {
                throw std::runtime_error("Missing required model input: " + input_name);
            }
            const Tensor& tensor = found->second;
            input_name_ptrs.push_back(input_name.c_str());
            switch (tensor.dtype) {
                case DType::Float32:
                    input_tensors.push_back(Ort::Value::CreateTensor<float>(
                        memory_info, const_cast<float*>(tensor.data_f32.data()),
                        tensor.data_f32.size(), tensor.shape.data(), tensor.shape.size()));
                    break;
                case DType::Int32:
                    input_tensors.push_back(Ort::Value::CreateTensor<int32_t>(
                        memory_info, const_cast<int32_t*>(tensor.data_i32.data()),
                        tensor.data_i32.size(), tensor.shape.data(), tensor.shape.size()));
                    break;
                case DType::Int64:
                    input_tensors.push_back(Ort::Value::CreateTensor<int64_t>(
                        memory_info, const_cast<int64_t*>(tensor.data_i64.data()),
                        tensor.data_i64.size(), tensor.shape.data(), tensor.shape.size()));
                    break;
                case DType::UInt8:
                    input_tensors.push_back(Ort::Value::CreateTensor<uint8_t>(
                        memory_info, const_cast<uint8_t*>(tensor.data_u8.data()),
                        tensor.data_u8.size(), tensor.shape.data(), tensor.shape.size()));
                    break;
            }
        }

        std::vector<std::string> output_names = requested_output_names.empty() ? output_names_ : requested_output_names;
        std::vector<const char*> output_name_ptrs;
        output_name_ptrs.reserve(output_names.size());
        for (const std::string& output_name : output_names) {
            output_name_ptrs.push_back(output_name.c_str());
        }

        auto outputs = session_->Run(
            Ort::RunOptions{nullptr},
            input_name_ptrs.data(), input_tensors.data(), input_tensors.size(),
            output_name_ptrs.data(), output_name_ptrs.size());

        std::unordered_map<std::string, Tensor> result;
        for (size_t index = 0; index < outputs.size(); ++index) {
            Ort::Value& output = outputs[index];
            const auto type_info = output.GetTensorTypeAndShapeInfo();
            const auto element_type = type_info.GetElementType();
            const std::vector<int64_t> shape = type_info.GetShape();
            const size_t count = tensor_numel(shape);
            Tensor tensor;
            tensor.shape = shape;
            switch (element_type) {
                case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT: {
                    tensor.dtype = DType::Float32;
                    const float* values = output.GetTensorData<float>();
                    tensor.data_f32.assign(values, values + count);
                    break;
                }
                case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32: {
                    tensor.dtype = DType::Int32;
                    const int32_t* values = output.GetTensorData<int32_t>();
                    tensor.data_i32.assign(values, values + count);
                    break;
                }
                case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64: {
                    tensor.dtype = DType::Int64;
                    const int64_t* values = output.GetTensorData<int64_t>();
                    tensor.data_i64.assign(values, values + count);
                    break;
                }
                case ONNX_TENSOR_ELEMENT_DATA_TYPE_BOOL:
                case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8: {
                    tensor.dtype = DType::UInt8;
                    const uint8_t* values = output.GetTensorData<uint8_t>();
                    tensor.data_u8.assign(values, values + count);
                    break;
                }
                default:
                    throw std::runtime_error("Unsupported ONNX output element type for " + output_names[index]);
            }
            result[output_names[index]] = std::move(tensor);
        }
        return result;
    }

    bool is_loaded() const override { return loaded_; }

private:
    Ort::Env env_;
    Ort::SessionOptions session_options_;
    std::unique_ptr<Ort::Session> session_;
    std::vector<std::string> input_names_;
    std::vector<std::string> output_names_;
    bool loaded_ = false;
};

std::unique_ptr<InferBackend> create_backend(const std::string& runtime) {
    if (runtime == "rknnruntime") {
        throw std::runtime_error("MOSS TTS does not support RKNN runtime (dynamic shapes)");
    }
    return std::make_unique<OnnxRuntimeBackend>();
}

}  // namespace moss_tts
