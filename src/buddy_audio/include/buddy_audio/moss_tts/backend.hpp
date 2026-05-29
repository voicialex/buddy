#pragma once

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace moss_tts {

enum class DType {
    Float32,
    Int32,
    Int64,
    UInt8,
};

struct Tensor {
    std::vector<float> data_f32;
    std::vector<int32_t> data_i32;
    std::vector<int64_t> data_i64;
    std::vector<uint8_t> data_u8;
    std::vector<int64_t> shape;
    DType dtype = DType::Float32;

    Tensor() = default;

    Tensor(std::vector<float> values, std::vector<int64_t> dims)
        : data_f32(std::move(values)), shape(std::move(dims)), dtype(DType::Float32) {}

    Tensor(std::vector<int32_t> values, std::vector<int64_t> dims)
        : data_i32(std::move(values)), shape(std::move(dims)), dtype(DType::Int32) {}

    Tensor(std::vector<int64_t> values, std::vector<int64_t> dims)
        : data_i64(std::move(values)), shape(std::move(dims)), dtype(DType::Int64) {}

    Tensor(std::vector<uint8_t> values, std::vector<int64_t> dims)
        : data_u8(std::move(values)), shape(std::move(dims)), dtype(DType::UInt8) {}

    size_t numel() const {
        size_t count = 1;
        for (int64_t dim : shape) {
            count *= static_cast<size_t>(dim);
        }
        return count;
    }

    const void* raw_data() const {
        switch (dtype) {
            case DType::Float32:
                return data_f32.data();
            case DType::Int32:
                return data_i32.data();
            case DType::Int64:
                return data_i64.data();
            case DType::UInt8:
                return data_u8.data();
        }
        throw std::runtime_error("Unsupported tensor dtype.");
    }
};

class InferBackend {
public:
    virtual ~InferBackend() = default;

    virtual void load_model(const std::string& model_path) = 0;

    virtual std::unordered_map<std::string, Tensor> run(
        const std::unordered_map<std::string, Tensor>& inputs,
        const std::vector<std::string>& output_names) = 0;

    virtual bool is_loaded() const = 0;
};

/// Create inference backend. runtime: "onnxruntime" (default), "rknnruntime" reserved.
std::unique_ptr<InferBackend> create_backend(const std::string& runtime = "onnxruntime");

}  // namespace moss_tts
