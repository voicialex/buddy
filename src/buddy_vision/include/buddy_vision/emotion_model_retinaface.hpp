#pragma once

#include <array>
#include <memory>
#include <string>
#include <vector>

#include <opencv2/opencv.hpp>
#include <rclcpp/rclcpp.hpp>

#include "buddy_vision/model_interface.hpp"
#include "buddy_vision/infer_backend.hpp"

struct FaceDetection {
    cv::Rect2f bbox;
    float confidence = 0.0f;
    std::array<cv::Point2f, 5> landmarks{};
};

class RetinaFaceDetector {
public:
    RetinaFaceDetector(std::unique_ptr<IInferenceBackend> backend, float confidence_threshold = 0.5f,
                       float nms_threshold = 0.4f);
    std::vector<FaceDetection> Detect(const cv::Mat& image) const;

private:
    struct Anchor {
        float cx, cy, sx, sy;
    };

    std::vector<float> Preprocess(const cv::Mat& image, cv::Mat& resized, float& scale) const;
    std::vector<Anchor> GenerateAnchors() const;

    std::unique_ptr<IInferenceBackend> backend_;
    float confidence_threshold_;
    float nms_threshold_;
    cv::Size input_size_{640, 640};
    std::vector<Anchor> anchors_;
};

class EmotionRecognizer {
public:
    explicit EmotionRecognizer(std::unique_ptr<IInferenceBackend> backend);
    std::string Predict(const cv::Mat& image, const std::array<cv::Point2f, 5>& landmarks,
                        float& confidence) const;

private:
    std::vector<float> Preprocess(const cv::Mat& image, const std::array<cv::Point2f, 5>& landmarks) const;

    std::unique_ptr<IInferenceBackend> backend_;
    cv::Size input_size_{112, 112};
};

// ModelInterface implementation using RetinaFace + AffectNet7
class FaceEmotionModel : public ModelInterface {
public:
    explicit FaceEmotionModel(rclcpp::Logger logger, const std::string& runtime = "onnxruntime");
    bool load(const std::string& model_dir) override;
    ModelResult inference(const cv::Mat& frame) override;
    void unload() override;

private:
    rclcpp::Logger logger_;
    std::string runtime_;
    std::unique_ptr<RetinaFaceDetector> detector_;
    std::unique_ptr<EmotionRecognizer> recognizer_;
};
