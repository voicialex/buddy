#include "buddy_audio/infer/backend.hpp"

#include <rknn_api.h>

#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace infer {

class RknnInferBackend final : public InferBackend {
public:
    ~RknnInferBackend() override {
        if (ctx_ != 0) {
            rknn_destroy(ctx_);
        }
    }

    void load_model(const std::string& model_path) override {
        std::ifstream file(model_path, std::ios::binary | std::ios::ate);
        if (!file) {
            throw std::runtime_error("Failed to open RKNN model: " + model_path);
        }
        const size_t size = static_cast<size_t>(file.tellg());
        file.seekg(0);
        model_data_.resize(size);
        file.read(reinterpret_cast<char*>(model_data_.data()), static_cast<std::streamsize>(size));

        if (rknn_init(&ctx_, model_data_.data(), model_data_.size(), 0, nullptr) != RKNN_SUCC) {
            throw std::runtime_error("rknn_init failed");
        }

        rknn_input_output_num io_num{};
        if (rknn_query(ctx_, RKNN_QUERY_IN_OUT_NUM, &io_num, sizeof(io_num)) != RKNN_SUCC) {
            throw std::runtime_error("rknn_query IN_OUT_NUM failed");
        }

        input_attrs_.resize(io_num.n_input);
        output_attrs_.resize(io_num.n_output);
        input_names_.resize(io_num.n_input);
        output_names_.resize(io_num.n_output);

        for (uint32_t i = 0; i < io_num.n_input; ++i) {
            input_attrs_[i].index = i;
            rknn_query(ctx_, RKNN_QUERY_INPUT_ATTR, &input_attrs_[i], sizeof(rknn_tensor_attr));
            input_names_[i] = input_attrs_[i].name ? input_attrs_[i].name : ("input_" + std::to_string(i));
        }
        for (uint32_t i = 0; i < io_num.n_output; ++i) {
            output_attrs_[i].index = i;
            rknn_query(ctx_, RKNN_QUERY_OUTPUT_ATTR, &output_attrs_[i], sizeof(rknn_tensor_attr));
            output_names_[i] = output_attrs_[i].name ? output_attrs_[i].name : ("output_" + std::to_string(i));
        }

        loaded_ = true;
    }

    TensorMap run(const TensorMap& inputs, const std::vector<std::string>&) override {
        if (!loaded_) {
            throw std::runtime_error("RKNN model not loaded");
        }

        std::vector<rknn_input> rknn_inputs(input_attrs_.size());
        for (size_t i = 0; i < input_attrs_.size(); ++i) {
            auto it = inputs.find(input_names_[i]);
            if (it == inputs.end()) {
                throw std::runtime_error("Missing RKNN input: " + input_names_[i]);
            }
            const Tensor& t = it->second;

            rknn_inputs[i].index = static_cast<uint32_t>(i);
            rknn_inputs[i].buf = const_cast<void*>(static_cast<const void*>(t.data.data()));
            rknn_inputs[i].size = static_cast<uint32_t>(t.data.size());
            rknn_inputs[i].fmt = input_attrs_[i].fmt;
            rknn_inputs[i].pass_through = 0;

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
                default:
                    rknn_inputs[i].type = RKNN_TENSOR_UINT8;
                    break;
            }
        }

        if (rknn_inputs_set(ctx_, rknn_inputs.size(), rknn_inputs.data()) != RKNN_SUCC) {
            throw std::runtime_error("rknn_inputs_set failed");
        }
        if (rknn_run(ctx_, nullptr) != RKNN_SUCC) {
            throw std::runtime_error("rknn_run failed");
        }

        std::vector<rknn_output> rknn_outs(output_attrs_.size());
        for (auto& o : rknn_outs) {
            o.want_float = 1;
            o.is_prealloc = 0;
        }
        if (rknn_outputs_get(ctx_, rknn_outs.size(), rknn_outs.data(), nullptr) != RKNN_SUCC) {
            throw std::runtime_error("rknn_outputs_get failed");
        }

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

private:
    rknn_context ctx_ = 0;
    std::vector<uint8_t> model_data_;
    std::vector<rknn_tensor_attr> input_attrs_;
    std::vector<rknn_tensor_attr> output_attrs_;
    std::vector<std::string> input_names_;
    std::vector<std::string> output_names_;
    bool loaded_ = false;
};

std::unique_ptr<InferBackend> create_rknn_backend() {
    return std::make_unique<RknnInferBackend>();
}

}  // namespace infer
