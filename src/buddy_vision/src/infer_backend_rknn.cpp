#include "buddy_vision/infer_backend.hpp"

#if HAS_RKNN

#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

#include <rknn_api.h>

namespace {

std::vector<float> ConvertNchwToNhwc(const std::vector<float>& input, int channels, int height, int width) {
    std::vector<float> reordered(static_cast<size_t>(channels) * height * width);
    for (int c = 0; c < channels; ++c) {
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const size_t src_index = static_cast<size_t>(c) * height * width + y * width + x;
                const size_t dst_index = static_cast<size_t>(y) * width * channels + x * channels + c;
                reordered[dst_index] = input[src_index];
            }
        }
    }
    return reordered;
}

class RknnBackend final : public IInferenceBackend {
public:
    explicit RknnBackend(const std::string& model_path) {
        std::ifstream file(model_path, std::ios::binary);
        if (!file) {
            throw std::runtime_error("Failed to open RKNN model: " + model_path);
        }

        file.seekg(0, std::ios::end);
        const size_t size = static_cast<size_t>(file.tellg());
        file.seekg(0, std::ios::beg);
        model_data_.resize(size);
        file.read(reinterpret_cast<char*>(model_data_.data()), static_cast<std::streamsize>(size));

        if (rknn_init(&ctx_, model_data_.data(), model_data_.size(), 0, nullptr) != RKNN_SUCC) {
            throw std::runtime_error("rknn_init failed");
        }

        rknn_input_output_num io_num{};
        if (rknn_query(ctx_, RKNN_QUERY_IN_OUT_NUM, &io_num, sizeof(io_num)) != RKNN_SUCC) {
            throw std::runtime_error("rknn_query IN_OUT_NUM failed");
        }
        input_attr_.index = 0;
        if (rknn_query(ctx_, RKNN_QUERY_INPUT_ATTR, &input_attr_, sizeof(rknn_tensor_attr)) != RKNN_SUCC) {
            throw std::runtime_error("rknn_query INPUT_ATTR failed");
        }
        output_attrs_.resize(io_num.n_output);
        for (uint32_t i = 0; i < io_num.n_output; ++i) {
            output_attrs_[i].index = i;
            if (rknn_query(ctx_, RKNN_QUERY_OUTPUT_ATTR, &output_attrs_[i], sizeof(rknn_tensor_attr)) != RKNN_SUCC) {
                throw std::runtime_error("rknn_query OUTPUT_ATTR failed");
            }
        }
    }

    ~RknnBackend() override {
        if (ctx_ != 0) {
            rknn_destroy(ctx_);
        }
    }

    RknnBackend(const RknnBackend&) = delete;
    RknnBackend& operator=(const RknnBackend&) = delete;

    std::vector<Tensor> Run(const std::vector<float>& input, const std::vector<int64_t>&) override {
        prepared_input_ = input;
        rknn_tensor_format input_format = input_attr_.fmt;
        if (input_format == RKNN_TENSOR_UNDEFINED) {
            input_format = RKNN_TENSOR_NHWC;
        }

        if (input_format == RKNN_TENSOR_NHWC && input_attr_.n_dims >= 4) {
            const int height = static_cast<int>(input_attr_.dims[1]);
            const int width = static_cast<int>(input_attr_.dims[2]);
            const int channels = static_cast<int>(input_attr_.dims[3]);
            const size_t expected_size = static_cast<size_t>(channels) * height * width;
            if (input.size() == expected_size) {
                prepared_input_ = ConvertNchwToNhwc(input, channels, height, width);
            }
        }

        rknn_input inputs[1]{};
        inputs[0].index = 0;
        inputs[0].buf = prepared_input_.data();
        inputs[0].size = prepared_input_.size() * sizeof(float);
        inputs[0].type = RKNN_TENSOR_FLOAT32;
        inputs[0].fmt = input_format;
        inputs[0].pass_through = 0;

        if (rknn_inputs_set(ctx_, 1, inputs) != RKNN_SUCC) {
            throw std::runtime_error("rknn_inputs_set failed");
        }
        if (rknn_run(ctx_, nullptr) != RKNN_SUCC) {
            throw std::runtime_error("rknn_run failed");
        }

        std::vector<rknn_output> outputs(output_attrs_.size());
        for (auto& output : outputs) {
            output.want_float = 1;
            output.is_prealloc = 0;
        }
        if (rknn_outputs_get(ctx_, outputs.size(), outputs.data(), nullptr) != RKNN_SUCC) {
            throw std::runtime_error("rknn_outputs_get failed");
        }

        std::vector<Tensor> result;
        result.reserve(outputs.size());
        for (size_t i = 0; i < outputs.size(); ++i) {
            Tensor tensor;
            const rknn_tensor_attr& attr = output_attrs_[i];
            tensor.shape.reserve(attr.n_dims);
            for (uint32_t d = 0; d < attr.n_dims; ++d) {
                tensor.shape.push_back(attr.dims[d]);
            }
            const float* data = reinterpret_cast<const float*>(outputs[i].buf);
            tensor.data.assign(data, data + attr.n_elems);
            result.push_back(std::move(tensor));
        }

        rknn_outputs_release(ctx_, outputs.size(), outputs.data());
        return result;
    }

private:
    rknn_context ctx_ = 0;
    std::vector<unsigned char> model_data_;
    rknn_tensor_attr input_attr_{};
    std::vector<rknn_tensor_attr> output_attrs_;
    std::vector<float> prepared_input_;
};

}  // namespace

std::unique_ptr<IInferenceBackend> CreateRknnBackend(const std::string& model_path) {
    return std::make_unique<RknnBackend>(model_path);
}

#endif  // HAS_RKNN
