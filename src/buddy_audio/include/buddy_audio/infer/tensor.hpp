#pragma once
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace infer {

enum class DType { Float32, Int32, Int64, UInt8 };

inline size_t dtype_size(DType dt) {
    switch (dt) {
        case DType::Float32:
            return 4;
        case DType::Int32:
            return 4;
        case DType::Int64:
            return 8;
        case DType::UInt8:
            return 1;
    }
    return 0;
}

struct Tensor {
    std::vector<int64_t> shape;
    DType dtype = DType::Float32;
    std::vector<uint8_t> data;

    size_t numel() const {
        size_t n = 1;
        for (auto d : shape) n *= static_cast<size_t>(d);
        return n;
    }

    size_t byte_size() const { return numel() * dtype_size(dtype); }

    template <typename T>
    T* ptr() {
        return reinterpret_cast<T*>(data.data());
    }

    template <typename T>
    const T* ptr() const {
        return reinterpret_cast<const T*>(data.data());
    }

    static Tensor from_float(std::vector<float> v, std::vector<int64_t> s) {
        Tensor t;
        t.shape = std::move(s);
        t.dtype = DType::Float32;
        t.data.resize(v.size() * sizeof(float));
        std::memcpy(t.data.data(), v.data(), t.data.size());
        return t;
    }

    static Tensor from_int64(std::vector<int64_t> v, std::vector<int64_t> s) {
        Tensor t;
        t.shape = std::move(s);
        t.dtype = DType::Int64;
        t.data.resize(v.size() * sizeof(int64_t));
        std::memcpy(t.data.data(), v.data(), t.data.size());
        return t;
    }

    static Tensor from_int32(std::vector<int32_t> v, std::vector<int64_t> s) {
        Tensor t;
        t.shape = std::move(s);
        t.dtype = DType::Int32;
        t.data.resize(v.size() * sizeof(int32_t));
        std::memcpy(t.data.data(), v.data(), t.data.size());
        return t;
    }
};

using TensorMap = std::unordered_map<std::string, Tensor>;

}  // namespace infer
