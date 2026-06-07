#include "buddy_audio/melo_rknn/backend.hpp"

#include <rknn_api.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <numeric>
#include <stdexcept>

#include <rclcpp/rclcpp.hpp>

namespace melo_tts {

namespace {

rclcpp::Logger melo_rknn_logger() {
    return rclcpp::get_logger("buddy_audio.melo_rknn");
}

std::vector<uint8_t> read_file(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Failed to open RKNN model: " + path);
    }
    input.seekg(0, std::ios::end);
    const auto size = input.tellg();
    input.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    input.read(reinterpret_cast<char*>(data.data()), size);
    return data;
}

rknn_tensor_type to_rknn_type(DType dtype) {
    if (dtype == DType::Float32) return RKNN_TENSOR_FLOAT32;
    if (dtype == DType::Int64) return RKNN_TENSOR_INT64;
    throw std::runtime_error("Unsupported RKNN input dtype");
}

size_t dtype_size(DType dtype) {
    if (dtype == DType::Float32) return sizeof(float);
    if (dtype == DType::Int64) return sizeof(int64_t);
    throw std::runtime_error("Unsupported dtype");
}

const char* dtype_name(DType dtype) {
    if (dtype == DType::Float32) return "float32";
    if (dtype == DType::Int64) return "int64";
    return "unknown";
}

std::string dims_string(const rknn_tensor_attr& attr) {
    std::string out = "[";
    for (uint32_t i = 0; i < attr.n_dims; ++i) {
        if (i > 0) out += ",";
        out += std::to_string(attr.dims[i]);
    }
    out += "]";
    return out;
}

void log_f32_stats(const std::string& prefix, const float* data, size_t count) {
    if (data == nullptr || count == 0) {
        RCLCPP_DEBUG(melo_rknn_logger(), "%s count=0", prefix.c_str());
        return;
    }
    float min_value = std::numeric_limits<float>::infinity();
    float max_value = -std::numeric_limits<float>::infinity();
    double sum = 0.0;
    double sum_sq = 0.0;
    size_t finite = 0;
    for (size_t i = 0; i < count; ++i) {
        const float v = data[i];
        if (!std::isfinite(v)) {
            continue;
        }
        min_value = std::min(min_value, v);
        max_value = std::max(max_value, v);
        sum += v;
        sum_sq += static_cast<double>(v) * static_cast<double>(v);
        ++finite;
    }
    const double mean = finite == 0 ? 0.0 : sum / static_cast<double>(finite);
    const double rms = finite == 0 ? 0.0 : std::sqrt(sum_sq / static_cast<double>(finite));
    RCLCPP_DEBUG(
        melo_rknn_logger(),
        "%s count=%zu finite=%zu min=%f max=%f mean=%f rms=%f",
        prefix.c_str(),
        count,
        finite,
        min_value,
        max_value,
        mean,
        rms);
}

}  // namespace

class RknnBackend final : public InferBackend {
public:
    ~RknnBackend() override {
        if (ctx_ != 0) {
            rknn_destroy(ctx_);
        }
    }

    void load_model(const std::string& model_path) override {
        model_data_ = read_file(model_path);
        int ret = rknn_init(&ctx_, model_data_.data(), model_data_.size(), 0, nullptr);
        if (ret != RKNN_SUCC) {
            throw std::runtime_error("rknn_init failed: " + std::to_string(ret));
        }
        rknn_input_output_num io_num{};
        ret = rknn_query(ctx_, RKNN_QUERY_IN_OUT_NUM, &io_num, sizeof(io_num));
        if (ret != RKNN_SUCC) {
            throw std::runtime_error("RKNN_QUERY_IN_OUT_NUM failed: " + std::to_string(ret));
        }
        input_attrs_.resize(io_num.n_input);
        output_attrs_.resize(io_num.n_output);
        for (uint32_t i = 0; i < io_num.n_input; ++i) {
            input_attrs_[i].index = i;
            rknn_query(ctx_, RKNN_QUERY_INPUT_ATTR, &input_attrs_[i], sizeof(rknn_tensor_attr));
            RCLCPP_DEBUG(
                melo_rknn_logger(),
                "[RKNN] input[%u] name=%s type=%s fmt=%s n_dims=%u dims=%s",
                i,
                input_attrs_[i].name,
                get_type_string(input_attrs_[i].type),
                get_format_string(input_attrs_[i].fmt),
                input_attrs_[i].n_dims,
                dims_string(input_attrs_[i]).c_str());
        }
        for (uint32_t i = 0; i < io_num.n_output; ++i) {
            output_attrs_[i].index = i;
            rknn_query(ctx_, RKNN_QUERY_OUTPUT_ATTR, &output_attrs_[i], sizeof(rknn_tensor_attr));
            RCLCPP_DEBUG(
                melo_rknn_logger(),
                "[RKNN] output[%u] name=%s type=%s fmt=%s n_dims=%u dims=%s",
                i,
                output_attrs_[i].name,
                get_type_string(output_attrs_[i].type),
                get_format_string(output_attrs_[i].fmt),
                output_attrs_[i].n_dims,
                dims_string(output_attrs_[i]).c_str());
        }
        RCLCPP_DEBUG(
            melo_rknn_logger(),
            "[RKNN] loaded %s inputs=%zu outputs=%zu",
            model_path.c_str(),
            input_attrs_.size(),
            output_attrs_.size());
    }

    std::unordered_map<std::string, Tensor> run(
        const std::unordered_map<std::string, Tensor>& inputs,
        const std::vector<std::string>& output_names) override {
        if (ctx_ == 0) {
            throw std::runtime_error("RKNN context is not loaded");
        }
        std::vector<rknn_input> rknn_inputs(input_attrs_.size());
        for (size_t i = 0; i < input_attrs_.size(); ++i) {
            std::string name = input_attrs_[i].name;
            auto it = inputs.find(name);
            if (it == inputs.end()) {
                static const std::vector<std::string> fallback_order = {
                    "x", "x_lengths", "sid", "tone", "language", "bert", "ja_bert",
                    "noise_scale", "length_scale", "noise_scale_w", "sdp_ratio",
                    "input_ids", "token_type_ids", "attention_mask",
                };
                if (i < fallback_order.size()) {
                    it = inputs.find(fallback_order[i]);
                    name = fallback_order[i];
                }
            }
            if (it == inputs.end()) {
                throw std::runtime_error("Missing RKNN input tensor: " + std::string(input_attrs_[i].name));
            }
            const Tensor& t = it->second;
            rknn_inputs[i].index = static_cast<uint32_t>(i);
            rknn_inputs[i].type = to_rknn_type(t.dtype);
            rknn_inputs[i].fmt = input_attrs_[i].fmt == RKNN_TENSOR_UNDEFINED
                ? RKNN_TENSOR_UNDEFINED
                : input_attrs_[i].fmt;
            rknn_inputs[i].buf = const_cast<void*>(t.dtype == DType::Float32
                ? static_cast<const void*>(t.f32.data())
                : static_cast<const void*>(t.i64.data()));
            rknn_inputs[i].size = static_cast<uint32_t>(t.numel() * dtype_size(t.dtype));
            rknn_inputs[i].pass_through = input_attrs_[i].type == rknn_inputs[i].type ? 1 : 0;
            RCLCPP_DEBUG(
                melo_rknn_logger(),
                "[RKNN] set input[%zu] %s dtype=%s model_type=%s pass_through=%d bytes=%u",
                i,
                name.c_str(),
                dtype_name(t.dtype),
                get_type_string(input_attrs_[i].type),
                static_cast<int>(rknn_inputs[i].pass_through),
                rknn_inputs[i].size);
        }
        int ret = rknn_inputs_set(ctx_, static_cast<uint32_t>(rknn_inputs.size()), rknn_inputs.data());
        if (ret != RKNN_SUCC) {
            throw std::runtime_error("rknn_inputs_set failed: " + std::to_string(ret));
        }
        ret = rknn_run(ctx_, nullptr);
        if (ret != RKNN_SUCC) {
            throw std::runtime_error("rknn_run failed: " + std::to_string(ret));
        }
        std::vector<rknn_output> outputs(output_attrs_.size());
        for (size_t i = 0; i < outputs.size(); ++i) {
            outputs[i].index = static_cast<uint32_t>(i);
            outputs[i].want_float = 1;
        }
        ret = rknn_outputs_get(ctx_, static_cast<uint32_t>(outputs.size()), outputs.data(), nullptr);
        if (ret != RKNN_SUCC) {
            throw std::runtime_error("rknn_outputs_get failed: " + std::to_string(ret));
        }

        std::unordered_map<std::string, Tensor> result;
        for (size_t i = 0; i < outputs.size(); ++i) {
            Tensor out;
            out.dtype = DType::Float32;
            out.shape.clear();
            for (uint32_t d = 0; d < output_attrs_[i].n_dims; ++d) {
                out.shape.push_back(output_attrs_[i].dims[d]);
            }
            const size_t count = outputs[i].size / sizeof(float);
            const float* data = reinterpret_cast<const float*>(outputs[i].buf);
            out.f32.assign(data, data + count);
            log_f32_stats(
                "[RKNN] output[" + std::to_string(i) + "] " + std::string(output_attrs_[i].name) +
                    " bytes=" + std::to_string(outputs[i].size),
                data,
                count);
            const std::string name = i < output_names.size() ? output_names[i] : ("output_" + std::to_string(i));
            result[name] = std::move(out);
        }
        rknn_outputs_release(ctx_, static_cast<uint32_t>(outputs.size()), outputs.data());
        return result;
    }

private:
    rknn_context ctx_ = 0;
    std::vector<uint8_t> model_data_;
    std::vector<rknn_tensor_attr> input_attrs_;
    std::vector<rknn_tensor_attr> output_attrs_;
};

std::unique_ptr<InferBackend> create_backend() {
    return std::make_unique<RknnBackend>();
}

}  // namespace melo_tts
