#include "buddy_vision/emotion_model_retinaface.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <numeric>
#include <stdexcept>

#include <rclcpp/rclcpp.hpp>

namespace {

constexpr std::array<const char*, 7> kEmotionLabels = {
    "Neutral", "Happy", "Sad", "Surprise", "Fear", "Disgust", "Angry"};

cv::Mat ResizeKeepAspectTopLeft(const cv::Mat& image, const cv::Size& target_size, float& scale) {
    const float image_ratio = static_cast<float>(image.rows) / static_cast<float>(image.cols);
    const float model_ratio = static_cast<float>(target_size.height) / static_cast<float>(target_size.width);

    int new_width = target_size.width;
    int new_height = target_size.height;
    if (image_ratio > model_ratio) {
        new_height = target_size.height;
        new_width = static_cast<int>(new_height / image_ratio);
    } else {
        new_width = target_size.width;
        new_height = static_cast<int>(new_width * image_ratio);
    }

    scale = static_cast<float>(new_height) / static_cast<float>(image.rows);

    cv::Mat resized;
    cv::resize(image, resized, cv::Size(new_width, new_height));

    cv::Mat canvas(target_size, CV_8UC3, cv::Scalar(0, 0, 0));
    resized.copyTo(canvas(cv::Rect(0, 0, resized.cols, resized.rows)));
    return canvas;
}

std::vector<int> NonMaxSuppression(const std::vector<cv::Rect2f>& boxes, const std::vector<float>& scores,
                                   float threshold) {
    std::vector<int> order(boxes.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](int lhs, int rhs) { return scores[lhs] > scores[rhs]; });

    std::vector<int> keep;
    while (!order.empty()) {
        const int best = order.front();
        keep.push_back(best);

        std::vector<int> remaining;
        for (size_t i = 1; i < order.size(); ++i) {
            const int idx = order[i];
            const float xx1 = std::max(boxes[best].x, boxes[idx].x);
            const float yy1 = std::max(boxes[best].y, boxes[idx].y);
            const float xx2 = std::min(boxes[best].x + boxes[best].width, boxes[idx].x + boxes[idx].width);
            const float yy2 = std::min(boxes[best].y + boxes[best].height, boxes[idx].y + boxes[idx].height);
            const float w = std::max(0.0f, xx2 - xx1 + 1.0f);
            const float h = std::max(0.0f, yy2 - yy1 + 1.0f);
            const float inter = w * h;
            const float area_a = (boxes[best].width + 1.0f) * (boxes[best].height + 1.0f);
            const float area_b = (boxes[idx].width + 1.0f) * (boxes[idx].height + 1.0f);
            const float union_area = area_a + area_b - inter;
            const float iou = union_area > 0.0f ? inter / union_area : 0.0f;
            if (iou <= threshold) {
                remaining.push_back(idx);
            }
        }
        order.swap(remaining);
    }
    return keep;
}

std::vector<float> Softmax(const std::vector<float>& logits) {
    const float max_logit = *std::max_element(logits.begin(), logits.end());
    std::vector<float> values(logits.size());
    float sum = 0.0f;
    for (size_t i = 0; i < logits.size(); ++i) {
        values[i] = std::exp(logits[i] - max_logit);
        sum += values[i];
    }
    for (float& value : values) {
        value /= sum;
    }
    return values;
}

cv::Mat AlignFace(const cv::Mat& image, const std::array<cv::Point2f, 5>& landmarks, const cv::Size& output_size) {
    static const std::array<cv::Point2f, 5> kReference = {
        cv::Point2f(38.2946f, 51.6963f), cv::Point2f(73.5318f, 51.5014f),
        cv::Point2f(56.0252f, 71.7366f), cv::Point2f(41.5493f, 92.3655f),
        cv::Point2f(70.7299f, 92.2041f),
    };

    std::vector<cv::Point2f> src(landmarks.begin(), landmarks.end());
    std::vector<cv::Point2f> dst(kReference.begin(), kReference.end());
    cv::Mat inliers;
    cv::Mat transform = cv::estimateAffinePartial2D(src, dst, inliers, cv::LMEDS);
    if (transform.empty()) {
        return cv::Mat();
    }

    cv::Mat aligned;
    cv::warpAffine(image, aligned, transform, output_size, cv::INTER_LINEAR, cv::BORDER_CONSTANT,
                   cv::Scalar(0, 0, 0));
    return aligned;
}

}  // namespace

// ============================================================
// RetinaFaceDetector
// ============================================================

RetinaFaceDetector::RetinaFaceDetector(std::unique_ptr<IInferenceBackend> backend, float confidence_threshold,
                                       float nms_threshold)
    : backend_(std::move(backend)),
      confidence_threshold_(confidence_threshold),
      nms_threshold_(nms_threshold),
      anchors_(GenerateAnchors()) {}

std::vector<float> RetinaFaceDetector::Preprocess(const cv::Mat& image, cv::Mat& resized, float& scale) const {
    resized = ResizeKeepAspectTopLeft(image, input_size_, scale);

    std::vector<float> input(3 * input_size_.width * input_size_.height);
    const std::array<float, 3> mean = {104.0f, 117.0f, 123.0f};

    for (int c = 0; c < 3; ++c) {
        for (int y = 0; y < resized.rows; ++y) {
            for (int x = 0; x < resized.cols; ++x) {
                input[c * resized.rows * resized.cols + y * resized.cols + x] =
                    static_cast<float>(resized.at<cv::Vec3b>(y, x)[c]) - mean[c];
            }
        }
    }
    return input;
}

std::vector<RetinaFaceDetector::Anchor> RetinaFaceDetector::GenerateAnchors() const {
    const std::array<int, 3> steps = {8, 16, 32};
    const std::array<std::array<int, 2>, 3> min_sizes = {{{16, 32}, {64, 128}, {256, 512}}};

    std::vector<Anchor> anchors;
    for (size_t level = 0; level < steps.size(); ++level) {
        const int step = steps[level];
        const int feature_h = static_cast<int>(std::ceil(static_cast<float>(input_size_.height) / step));
        const int feature_w = static_cast<int>(std::ceil(static_cast<float>(input_size_.width) / step));

        for (int i = 0; i < feature_h; ++i) {
            for (int j = 0; j < feature_w; ++j) {
                for (int min_size : min_sizes[level]) {
                    anchors.push_back({(j + 0.5f) * step / static_cast<float>(input_size_.width),
                                       (i + 0.5f) * step / static_cast<float>(input_size_.height),
                                       min_size / static_cast<float>(input_size_.width),
                                       min_size / static_cast<float>(input_size_.height)});
                }
            }
        }
    }
    return anchors;
}

std::vector<FaceDetection> RetinaFaceDetector::Detect(const cv::Mat& image) const {
    cv::Mat resized;
    float scale = 1.0f;
    const std::vector<float> input = Preprocess(image, resized, scale);
    const std::vector<Tensor> outputs = backend_->Run(input, {1, 3, input_size_.height, input_size_.width});
    if (outputs.size() < 3) {
        return {};
    }

    const size_t anchor_count = anchors_.size();
    const Tensor* loc = nullptr;
    const Tensor* conf = nullptr;
    const Tensor* landm = nullptr;

    for (const auto& output : outputs) {
        if (output.data.size() == anchor_count * 4 && loc == nullptr) {
            loc = &output;
        } else if (output.data.size() == anchor_count * 2 && conf == nullptr) {
            conf = &output;
        } else if (output.data.size() == anchor_count * 10 && landm == nullptr) {
            landm = &output;
        }
    }

    if (loc == nullptr || conf == nullptr || landm == nullptr) {
        return {};
    }

    std::vector<cv::Rect2f> boxes;
    std::vector<std::array<cv::Point2f, 5>> landmarks;
    std::vector<float> scores;

    for (size_t i = 0; i < anchor_count; ++i) {
        const float score = conf->data[i * 2 + 1];
        if (score <= confidence_threshold_) {
            continue;
        }

        const auto& prior = anchors_[i];
        const float cx = prior.cx + loc->data[i * 4 + 0] * 0.1f * prior.sx;
        const float cy = prior.cy + loc->data[i * 4 + 1] * 0.1f * prior.sy;
        const float w = prior.sx * std::exp(loc->data[i * 4 + 2] * 0.2f);
        const float h = prior.sy * std::exp(loc->data[i * 4 + 3] * 0.2f);

        float x1 = (cx - w / 2.0f) * input_size_.width / scale;
        float y1 = (cy - h / 2.0f) * input_size_.height / scale;
        float x2 = (cx + w / 2.0f) * input_size_.width / scale;
        float y2 = (cy + h / 2.0f) * input_size_.height / scale;

        boxes.emplace_back(x1, y1, x2 - x1, y2 - y1);
        scores.push_back(score);

        std::array<cv::Point2f, 5> points{};
        for (size_t p = 0; p < 5; ++p) {
            const float px =
                (prior.cx + landm->data[i * 10 + p * 2 + 0] * 0.1f * prior.sx) * input_size_.width / scale;
            const float py =
                (prior.cy + landm->data[i * 10 + p * 2 + 1] * 0.1f * prior.sy) * input_size_.height / scale;
            points[p] = cv::Point2f(px, py);
        }
        landmarks.push_back(points);
    }

    const std::vector<int> keep = NonMaxSuppression(boxes, scores, nms_threshold_);
    std::vector<FaceDetection> faces;
    faces.reserve(keep.size());
    for (int idx : keep) {
        FaceDetection face;
        face.bbox = boxes[idx];
        face.confidence = scores[idx];
        face.landmarks = landmarks[idx];
        faces.push_back(face);
    }
    return faces;
}

// ============================================================
// EmotionRecognizer
// ============================================================

EmotionRecognizer::EmotionRecognizer(std::unique_ptr<IInferenceBackend> backend) : backend_(std::move(backend)) {}

std::vector<float> EmotionRecognizer::Preprocess(const cv::Mat& image,
                                                 const std::array<cv::Point2f, 5>& landmarks) const {
    const cv::Mat aligned = AlignFace(image, landmarks, input_size_);
    if (aligned.empty()) {
        return {};
    }

    cv::Mat rgb;
    cv::cvtColor(aligned, rgb, cv::COLOR_BGR2RGB);

    std::vector<float> input(3 * input_size_.width * input_size_.height);
    const std::array<float, 3> mean = {0.485f, 0.456f, 0.406f};
    const std::array<float, 3> std_val = {0.229f, 0.224f, 0.225f};

    for (int c = 0; c < 3; ++c) {
        for (int y = 0; y < rgb.rows; ++y) {
            for (int x = 0; x < rgb.cols; ++x) {
                const float value = static_cast<float>(rgb.at<cv::Vec3b>(y, x)[c]) / 255.0f;
                input[c * rgb.rows * rgb.cols + y * rgb.cols + x] = (value - mean[c]) / std_val[c];
            }
        }
    }
    return input;
}

std::string EmotionRecognizer::Predict(const cv::Mat& image, const std::array<cv::Point2f, 5>& landmarks,
                                       float& confidence) const {
    const std::vector<float> input = Preprocess(image, landmarks);
    if (input.empty()) {
        confidence = 0.0f;
        return "Unknown";
    }

    const std::vector<Tensor> outputs = backend_->Run(input, {1, 3, input_size_.height, input_size_.width});
    if (outputs.empty() || outputs[0].data.empty()) {
        confidence = 0.0f;
        return "Unknown";
    }

    const std::vector<float> probs = Softmax(outputs[0].data);
    const auto best = std::max_element(probs.begin(), probs.end());
    const size_t best_index = static_cast<size_t>(std::distance(probs.begin(), best));

    confidence = *best;
    return kEmotionLabels.at(best_index);
}

// ============================================================
// FaceEmotionModel (ModelInterface)
// ============================================================

FaceEmotionModel::FaceEmotionModel(rclcpp::Logger logger, const std::string& runtime)
    : logger_(logger), runtime_(runtime) {}

bool FaceEmotionModel::load(const std::string& model_dir) {
    namespace fs = std::filesystem;

    std::string det_ext = ".onnx";
    std::string emo_ext = ".onnx";
    bool use_rknn = false;

#if HAS_RKNN
    if (runtime_ == "rknnruntime" || runtime_ == "auto") {
        const fs::path rknn_det = fs::path(model_dir) / "retinaface_mnet_v2_fp16.rknn";
        const fs::path rknn_emo = fs::path(model_dir) / "affecnet7_fp16.rknn";
        if (fs::exists(rknn_det) && fs::exists(rknn_emo)) {
            use_rknn = true;
            det_ext = ".rknn";
            emo_ext = ".rknn";
        }
    }
#endif

    const std::string det_path = (fs::path(model_dir) / ("retinaface_mnet_v2_fp16" + det_ext)).string();
    const std::string emo_path = (fs::path(model_dir) / ("affecnet7_fp16" + emo_ext)).string();

    if (!fs::exists(det_path) || !fs::exists(emo_path)) {
        RCLCPP_ERROR(logger_, "Face emotion models not found in %s", model_dir.c_str());
        return false;
    }

    try {
        std::unique_ptr<IInferenceBackend> det_backend;
        std::unique_ptr<IInferenceBackend> emo_backend;

        if (use_rknn) {
#if HAS_RKNN
            det_backend = CreateRknnBackend(det_path);
            emo_backend = CreateRknnBackend(emo_path);
            RCLCPP_INFO(logger_, "Face emotion: using RKNN backend");
#endif
        } else {
            det_backend = CreateOnnxRuntimeBackend(det_path, true);
            emo_backend = CreateOnnxRuntimeBackend(emo_path, true);
            RCLCPP_INFO(logger_, "Face emotion: using ORT backend");
        }

        detector_ = std::make_unique<RetinaFaceDetector>(std::move(det_backend));
        recognizer_ = std::make_unique<EmotionRecognizer>(std::move(emo_backend));
    } catch (const std::exception& e) {
        RCLCPP_ERROR(logger_, "Failed to load face emotion models: %s", e.what());
        return false;
    }

    RCLCPP_INFO(logger_, "Face emotion pipeline loaded from %s", model_dir.c_str());
    return true;
}

ModelResult FaceEmotionModel::inference(const cv::Mat& frame) {
    ModelResult result;
    result.label = "Unknown";
    result.confidence = 0.0f;

    if (!detector_ || !recognizer_) {
        return result;
    }

    const auto faces = detector_->Detect(frame);
    if (faces.empty()) {
        return result;
    }

    // Use largest face
    const auto& face = *std::max_element(faces.begin(), faces.end(), [](const FaceDetection& a, const FaceDetection& b) {
        return a.bbox.area() < b.bbox.area();
    });

    float conf = 0.0f;
    std::string emotion = recognizer_->Predict(frame, face.landmarks, conf);

    result.label = emotion;
    result.confidence = conf;
    result.face_rect = cv::Rect(static_cast<int>(face.bbox.x), static_cast<int>(face.bbox.y),
                                static_cast<int>(face.bbox.width), static_cast<int>(face.bbox.height));
    return result;
}

void FaceEmotionModel::unload() {
    detector_.reset();
    recognizer_.reset();
}
