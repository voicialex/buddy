#pragma once

#include <cstdint>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

namespace melo_tts {

enum class DType {
    Float32,
    Int64,
};

struct Tensor {
    DType dtype = DType::Float32;
    std::vector<int64_t> shape;
    std::vector<float> f32;
    std::vector<int64_t> i64;

    size_t numel() const {
        if (shape.empty()) {
            return 1;
        }
        return static_cast<size_t>(
            std::accumulate(shape.begin(), shape.end(), int64_t{1}, std::multiplies<int64_t>()));
    }

    void validate() const {
        const size_t n = numel();
        if (dtype == DType::Float32 && f32.size() != n) {
            throw std::runtime_error("float tensor data size does not match shape");
        }
        if (dtype == DType::Int64 && i64.size() != n) {
            throw std::runtime_error("int64 tensor data size does not match shape");
        }
    }
};

}  // namespace melo_tts
