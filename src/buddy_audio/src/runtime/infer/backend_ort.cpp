#include "buddy_audio/runtime/infer/backend.hpp"

#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <numeric>
#include <stdexcept>

namespace infer {

namespace {

size_t tensor_numel(const std::vector<int64_t>& shape) {
    return static_cast<size_t>(
        std::accumulate(shape.begin(), shape.end(), int64_t(1), std::multiplies<int64_t>()));
}

}  // namespace

class OrtInferBackend : public InferBackend {
public:
    OrtInferBackend()
        : env_(ORT_LOGGING_LEVEL_WARNING, "infer_ort") {
        session_options_.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        session_options_.SetIntraOpNumThreads(4);
        session_options_.SetInterOpNumThreads(1);

        // Auto-detect CUDA if available
        try {
            OrtCUDAProviderOptions cuda_options{};
            cuda_options.device_id = 0;
            session_options_.AppendExecutionProvider_CUDA(cuda_options);
        } catch (...) {
            // CUDA not available, fallback to CPU
        }
    }

    void load_model(const std::string& model_path) override {
        current_model_path_ = model_path;
        try {
            session_ = std::make_unique<Ort::Session>(env_, model_path.c_str(), session_options_);
        } catch (const std::exception& e) {
            throw std::runtime_error(
                "Failed to load ORT model: " + current_model_path_ + " error: " + e.what());
        }
        loaded_ = true;

        Ort::AllocatorWithDefaultOptions allocator;
        input_names_.clear();
        output_names_.clear();
        for (size_t i = 0; i < session_->GetInputCount(); ++i) {
            auto name = session_->GetInputNameAllocated(i, allocator);
            input_names_.emplace_back(name.get());
        }
        for (size_t i = 0; i < session_->GetOutputCount(); ++i) {
            auto name = session_->GetOutputNameAllocated(i, allocator);
            output_names_.emplace_back(name.get());
        }
    }

    TensorMap run(const TensorMap& inputs, const std::vector<std::string>& requested_outputs) override {
        if (!loaded_) {
            throw std::runtime_error("ORT model not loaded");
        }

        Ort::MemoryInfo mem_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        std::vector<const char*> input_name_ptrs;
        std::vector<Ort::Value> input_tensors;
        input_name_ptrs.reserve(input_names_.size());
        input_tensors.reserve(input_names_.size());

        for (const auto& name : input_names_) {
            auto it = inputs.find(name);
            if (it == inputs.end()) {
                throw std::runtime_error("Missing input: " + name + " for ORT model: " + current_model_path_);
            }
            const Tensor& t = it->second;
            input_name_ptrs.push_back(name.c_str());

            size_t count = t.numel();
            switch (t.dtype) {
                case DType::Float32:
                    input_tensors.push_back(Ort::Value::CreateTensor<float>(
                        mem_info, const_cast<float*>(t.ptr<float>()), count,
                        t.shape.data(), t.shape.size()));
                    break;
                case DType::Int32:
                    input_tensors.push_back(Ort::Value::CreateTensor<int32_t>(
                        mem_info, const_cast<int32_t*>(t.ptr<int32_t>()), count,
                        t.shape.data(), t.shape.size()));
                    break;
                case DType::Int64:
                    input_tensors.push_back(Ort::Value::CreateTensor<int64_t>(
                        mem_info, const_cast<int64_t*>(t.ptr<int64_t>()), count,
                        t.shape.data(), t.shape.size()));
                    break;
                case DType::UInt8:
                    input_tensors.push_back(Ort::Value::CreateTensor<uint8_t>(
                        mem_info, const_cast<uint8_t*>(t.ptr<uint8_t>()), count,
                        t.shape.data(), t.shape.size()));
                    break;
            }
        }

        const auto& out_names = requested_outputs.empty() ? output_names_ : requested_outputs;
        std::vector<const char*> out_ptrs;
        out_ptrs.reserve(out_names.size());
        for (const auto& n : out_names) out_ptrs.push_back(n.c_str());

        auto outputs = session_->Run(
            Ort::RunOptions{nullptr},
            input_name_ptrs.data(), input_tensors.data(), input_tensors.size(),
            out_ptrs.data(), out_ptrs.size());

        TensorMap result;
        for (size_t i = 0; i < outputs.size(); ++i) {
            auto& val = outputs[i];
            auto type_info = val.GetTensorTypeAndShapeInfo();
            auto elem_type = type_info.GetElementType();
            auto shape = type_info.GetShape();
            size_t count = tensor_numel(shape);

            Tensor t;
            t.shape = shape;
            switch (elem_type) {
                case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT: {
                    t.dtype = DType::Float32;
                    const float* p = val.GetTensorData<float>();
                    t.data.resize(count * sizeof(float));
                    std::memcpy(t.data.data(), p, t.data.size());
                    break;
                }
                case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32: {
                    t.dtype = DType::Int32;
                    const int32_t* p = val.GetTensorData<int32_t>();
                    t.data.resize(count * sizeof(int32_t));
                    std::memcpy(t.data.data(), p, t.data.size());
                    break;
                }
                case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64: {
                    t.dtype = DType::Int64;
                    const int64_t* p = val.GetTensorData<int64_t>();
                    t.data.resize(count * sizeof(int64_t));
                    std::memcpy(t.data.data(), p, t.data.size());
                    break;
                }
                case ONNX_TENSOR_ELEMENT_DATA_TYPE_BOOL:
                case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8: {
                    t.dtype = DType::UInt8;
                    const uint8_t* p = val.GetTensorData<uint8_t>();
                    t.data.resize(count);
                    std::memcpy(t.data.data(), p, count);
                    break;
                }
                default:
                    throw std::runtime_error("Unsupported ORT output type for: " + out_names[i]);
            }
            result[out_names[i]] = std::move(t);
        }
        return result;
    }

    bool is_loaded() const override { return loaded_; }
    bool get_input_spec(const std::string& name, InputSpec* out) const override {
        if (!loaded_ || !out || !session_) {
            return false;
        }
        size_t idx = input_names_.size();
        for (size_t i = 0; i < input_names_.size(); ++i) {
            if (input_names_[i] == name) {
                idx = i;
                break;
            }
        }
        if (idx == input_names_.size()) {
            return false;
        }

        auto info = session_->GetInputTypeInfo(idx).GetTensorTypeAndShapeInfo();
        out->shape = info.GetShape();
        for (auto& d : out->shape) {
            if (d < 0) {
                d = 0;
            }
        }
        const auto elem_type = info.GetElementType();
        switch (elem_type) {
            case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64:
                out->dtype = DType::Int64;
                break;
            case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32:
                out->dtype = DType::Int32;
                break;
            case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8:
            case ONNX_TENSOR_ELEMENT_DATA_TYPE_BOOL:
                out->dtype = DType::UInt8;
                break;
            default:
                out->dtype = DType::Float32;
                break;
        }
        const size_t count = tensor_numel(out->shape);
        out->required_elems = count;
        out->size = count * dtype_size(out->dtype);
        out->size_with_stride = out->size;
        return true;
    }

private:
    Ort::Env env_;
    Ort::SessionOptions session_options_;
    std::unique_ptr<Ort::Session> session_;
    std::vector<std::string> input_names_;
    std::vector<std::string> output_names_;
    std::string current_model_path_;
    bool loaded_ = false;
};

std::unique_ptr<InferBackend> create_ort_backend() {
    return std::make_unique<OrtInferBackend>();
}

}  // namespace infer
