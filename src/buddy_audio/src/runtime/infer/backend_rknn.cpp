#include "buddy_audio/runtime/infer/backend.hpp"

#include <rknn_api.h>
#include <rcutils/logging_macros.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace infer {

namespace {
constexpr const char* kRknnLogName = "buddy_audio.rknn_backend";

size_t dtype_bytes(DType dt) {
    switch (dt) {
        case DType::Float32:
            return 4;
        case DType::Int64:
            return 8;
        case DType::Int32:
            return 4;
        case DType::UInt8:
        default:
            return 1;
    }
}

uint16_t float32_to_fp16_bits(float value) {
    uint32_t x = 0;
    std::memcpy(&x, &value, sizeof(x));
    const uint32_t sign = (x >> 16) & 0x8000u;
    uint32_t mantissa = x & 0x007fffffu;
    int32_t exp = static_cast<int32_t>((x >> 23) & 0xffu) - 127 + 15;

    if (exp <= 0) {
        if (exp < -10) {
            return static_cast<uint16_t>(sign);
        }
        mantissa = (mantissa | 0x00800000u) >> static_cast<uint32_t>(1 - exp);
        if (mantissa & 0x00001000u) {
            mantissa += 0x00002000u;
        }
        return static_cast<uint16_t>(sign | (mantissa >> 13));
    }

    if (exp >= 31) {
        return static_cast<uint16_t>(sign | 0x7c00u);
    }

    if (mantissa & 0x00001000u) {
        mantissa += 0x00002000u;
        if (mantissa & 0x00800000u) {
            mantissa = 0;
            ++exp;
            if (exp >= 31) {
                return static_cast<uint16_t>(sign | 0x7c00u);
            }
        }
    }
    return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exp) << 10) | (mantissa >> 13));
}

DType rknn_to_dtype(rknn_tensor_type type) {
    switch (type) {
        case RKNN_TENSOR_INT64:
            return DType::Int64;
        case RKNN_TENSOR_INT32:
            return DType::Int32;
        case RKNN_TENSOR_UINT8:
        case RKNN_TENSOR_INT8:
        case RKNN_TENSOR_BOOL:
            return DType::UInt8;
        default:
            return DType::Float32;
    }
}

}  // namespace

class RknnInferBackend final : public InferBackend {
public:
    ~RknnInferBackend() override {
        if (ctx_ != 0) {
            rknn_destroy(ctx_);
        }
    }

    void load_model(const std::string& model_path) override {
        current_model_path_ = model_path;
        RCUTILS_LOG_INFO_NAMED(kRknnLogName, "loading model: %s", current_model_path_.c_str());
        std::ifstream file(model_path, std::ios::binary | std::ios::ate);
        if (!file) {
            throw std::runtime_error("Failed to open RKNN model: " + model_path);
        }
        const size_t size = static_cast<size_t>(file.tellg());
        file.seekg(0);
        model_data_.resize(size);
        file.read(reinterpret_cast<char*>(model_data_.data()), static_cast<std::streamsize>(size));

        const int init_ret = rknn_init(&ctx_, model_data_.data(), model_data_.size(), 0, nullptr);
        if (init_ret != RKNN_SUCC) {
            throw std::runtime_error(
                "rknn_init failed for model: " + current_model_path_ + " ret=" + std::to_string(init_ret));
        }

        rknn_input_output_num io_num{};
        if (rknn_query(ctx_, RKNN_QUERY_IN_OUT_NUM, &io_num, sizeof(io_num)) != RKNN_SUCC) {
            throw std::runtime_error("rknn_query IN_OUT_NUM failed for model: " + current_model_path_);
        }

        input_attrs_.resize(io_num.n_input);
        output_attrs_.resize(io_num.n_output);
        input_names_.resize(io_num.n_input);
        output_names_.resize(io_num.n_output);

        for (uint32_t i = 0; i < io_num.n_input; ++i) {
            input_attrs_[i].index = i;
            if (rknn_query(ctx_, RKNN_QUERY_INPUT_ATTR, &input_attrs_[i], sizeof(rknn_tensor_attr)) != RKNN_SUCC) {
                throw std::runtime_error(
                    "rknn_query INPUT_ATTR failed for model: " + current_model_path_ +
                    " at index " + std::to_string(i));
            }
            input_names_[i] = input_attrs_[i].name ? input_attrs_[i].name : ("input_" + std::to_string(i));
        }
        for (uint32_t i = 0; i < io_num.n_output; ++i) {
            output_attrs_[i].index = i;
            if (rknn_query(ctx_, RKNN_QUERY_OUTPUT_ATTR, &output_attrs_[i], sizeof(rknn_tensor_attr)) != RKNN_SUCC) {
                throw std::runtime_error(
                    "rknn_query OUTPUT_ATTR failed for model: " + current_model_path_ +
                    " at index " + std::to_string(i));
            }
            output_names_[i] = output_attrs_[i].name ? output_attrs_[i].name : ("output_" + std::to_string(i));
        }

        loaded_ = true;
        RCUTILS_LOG_INFO_NAMED(
            kRknnLogName, "model ready: %s (inputs=%zu, outputs=%zu)",
            current_model_path_.c_str(), input_attrs_.size(), output_attrs_.size());
        for (size_t i = 0; i < input_attrs_.size(); ++i) {
            const auto& a = input_attrs_[i];
            RCUTILS_LOG_DEBUG_NAMED(
                kRknnLogName,
                "input[%zu] name=%s type=%d fmt=%d n_elems=%u size=%u size_with_stride=%u n_dims=%u",
                i,
                input_names_[i].c_str(),
                static_cast<int>(a.type),
                static_cast<int>(a.fmt),
                a.n_elems,
                a.size,
                a.size_with_stride,
                a.n_dims);
        }
    }

    TensorMap run(const TensorMap& inputs, const std::vector<std::string>&) override {
        if (!loaded_) {
            throw std::runtime_error("RKNN model not loaded");
        }

        std::vector<rknn_input> rknn_inputs(input_attrs_.size());
        std::vector<std::vector<uint8_t>> converted_inputs(input_attrs_.size());
        const uint64_t run_id = ++run_counter_;
        RCUTILS_LOG_DEBUG_NAMED(
            kRknnLogName, "run#%llu begin model=%s",
            static_cast<unsigned long long>(run_id), current_model_path_.c_str());
        for (size_t i = 0; i < input_attrs_.size(); ++i) {
            auto it = inputs.find(input_names_[i]);
            if (it == inputs.end()) {
                throw std::runtime_error("Missing RKNN input: " + input_names_[i]);
            }
            const Tensor& t = it->second;
            const size_t expected_elems = static_cast<size_t>(input_attrs_[i].n_elems);
            rknn_inputs[i].index = static_cast<uint32_t>(i);
            rknn_inputs[i].fmt = input_attrs_[i].fmt;
            const size_t input_elems = t.numel();
            if (input_elems != expected_elems) {
                throw std::runtime_error(
                    "RKNN input elem mismatch for model: " + current_model_path_ + " input=" + input_names_[i] +
                    " expected=" + std::to_string(expected_elems) +
                    " got=" + std::to_string(input_elems));
            }

            // For FP16 models, convert float32 tensors explicitly. Keep pass_through=0
            // so RKNN runtime can handle internal layout/stride safely.
            if (input_attrs_[i].type == RKNN_TENSOR_FLOAT16) {
                if (t.dtype != DType::Float32) {
                    throw std::runtime_error(
                        "RKNN FP16 input requires float32 source tensor for model: " + current_model_path_ +
                        " input=" + input_names_[i]);
                }
                converted_inputs[i].resize(expected_elems * sizeof(uint16_t));
                auto* out = reinterpret_cast<uint16_t*>(converted_inputs[i].data());
                const auto* in = reinterpret_cast<const float*>(t.data.data());
                for (size_t j = 0; j < expected_elems; ++j) {
                    out[j] = float32_to_fp16_bits(in[j]);
                }
                rknn_inputs[i].buf = converted_inputs[i].data();
                rknn_inputs[i].size = static_cast<uint32_t>(converted_inputs[i].size());
                rknn_inputs[i].type = RKNN_TENSOR_FLOAT16;
                rknn_inputs[i].pass_through = 0;
                continue;
            }

            rknn_inputs[i].buf = const_cast<void*>(static_cast<const void*>(t.data.data()));
            rknn_inputs[i].size = static_cast<uint32_t>(t.data.size());
            switch (t.dtype) {
                case DType::Float32:
                    rknn_inputs[i].type = RKNN_TENSOR_FLOAT32;
                    break;
                case DType::Int64:
                    rknn_inputs[i].type = RKNN_TENSOR_INT64;
                    break;
                case DType::Int32:
                    rknn_inputs[i].type = RKNN_TENSOR_INT32;
                    break;
                case DType::UInt8:
                default:
                    rknn_inputs[i].type = RKNN_TENSOR_UINT8;
                    break;
            }
            rknn_inputs[i].pass_through = (rknn_inputs[i].type == input_attrs_[i].type) ? 1 : 0;
            const size_t expected_bytes = expected_elems * dtype_bytes(t.dtype);
            if (expected_bytes != t.data.size()) {
                throw std::runtime_error(
                    "RKNN input byte mismatch for model: " + current_model_path_ + " input=" + input_names_[i] +
                    " expected=" + std::to_string(expected_bytes) +
                    " got=" + std::to_string(t.data.size()));
            }
        }

        for (size_t i = 0; i < rknn_inputs.size(); ++i) {
            RCUTILS_LOG_DEBUG_NAMED(
                kRknnLogName,
                "run#%llu send_input[%zu] name=%s type=%d pass_through=%d size=%u expect_size=%u expect_stride=%u",
                static_cast<unsigned long long>(run_id),
                i,
                input_names_[i].c_str(),
                static_cast<int>(rknn_inputs[i].type),
                static_cast<int>(rknn_inputs[i].pass_through),
                rknn_inputs[i].size,
                input_attrs_[i].size,
                input_attrs_[i].size_with_stride);
        }

        RCUTILS_LOG_DEBUG_NAMED(
            kRknnLogName, "run#%llu call rknn_inputs_set",
            static_cast<unsigned long long>(run_id));
        if (rknn_inputs_set(ctx_, rknn_inputs.size(), rknn_inputs.data()) != RKNN_SUCC) {
            throw std::runtime_error("rknn_inputs_set failed for model: " + current_model_path_);
        }
        RCUTILS_LOG_DEBUG_NAMED(
            kRknnLogName, "run#%llu rknn_inputs_set ok, call rknn_run",
            static_cast<unsigned long long>(run_id));
        if (rknn_run(ctx_, nullptr) != RKNN_SUCC) {
            throw std::runtime_error("rknn_run failed for model: " + current_model_path_);
        }
        RCUTILS_LOG_DEBUG_NAMED(
            kRknnLogName, "run#%llu rknn_run ok, call rknn_outputs_get",
            static_cast<unsigned long long>(run_id));

        std::vector<rknn_output> rknn_outs(output_attrs_.size());
        for (auto& o : rknn_outs) {
            o.want_float = 1;
            o.is_prealloc = 0;
        }
        if (rknn_outputs_get(ctx_, rknn_outs.size(), rknn_outs.data(), nullptr) != RKNN_SUCC) {
            throw std::runtime_error("rknn_outputs_get failed for model: " + current_model_path_);
        }
        RCUTILS_LOG_DEBUG_NAMED(
            kRknnLogName, "run#%llu rknn_outputs_get ok",
            static_cast<unsigned long long>(run_id));

        TensorMap result;
        for (size_t i = 0; i < rknn_outs.size(); ++i) {
            Tensor t;
            t.dtype = DType::Float32;
            t.shape.reserve(output_attrs_[i].n_dims);
            for (uint32_t d = 0; d < output_attrs_[i].n_dims; ++d) {
                t.shape.push_back(static_cast<int64_t>(output_attrs_[i].dims[d]));
            }
            size_t byte_count = output_attrs_[i].n_elems * sizeof(float);
            t.data.resize(byte_count);
            std::memcpy(t.data.data(), rknn_outs[i].buf, byte_count);
            result[output_names_[i]] = std::move(t);
        }

        rknn_outputs_release(ctx_, rknn_outs.size(), rknn_outs.data());
        return result;
    }

    bool is_loaded() const override { return loaded_; }
    bool get_input_spec(const std::string& name, InputSpec* out) const override {
        if (!out) {
            return false;
        }
        for (size_t i = 0; i < input_names_.size(); ++i) {
            if (input_names_[i] != name) {
                continue;
            }
            out->shape.clear();
            out->shape.reserve(input_attrs_[i].n_dims);
            for (uint32_t d = 0; d < input_attrs_[i].n_dims; ++d) {
                out->shape.push_back(static_cast<int64_t>(input_attrs_[i].dims[d]));
            }
            out->dtype = rknn_to_dtype(input_attrs_[i].type);
            out->required_elems = static_cast<size_t>(input_attrs_[i].n_elems);
            out->size = static_cast<size_t>(input_attrs_[i].size);
            out->size_with_stride = static_cast<size_t>(input_attrs_[i].size_with_stride);
            return true;
        }
        return false;
    }

private:
    rknn_context ctx_ = 0;
    std::vector<uint8_t> model_data_;
    std::vector<rknn_tensor_attr> input_attrs_;
    std::vector<rknn_tensor_attr> output_attrs_;
    std::vector<std::string> input_names_;
    std::vector<std::string> output_names_;
    std::string current_model_path_;
    std::atomic<uint64_t> run_counter_{0};
    bool loaded_ = false;
};

std::unique_ptr<InferBackend> create_rknn_backend() {
    return std::make_unique<RknnInferBackend>();
}

}  // namespace infer
