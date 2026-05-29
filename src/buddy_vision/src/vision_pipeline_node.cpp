#include "buddy_vision/vision_pipeline_node.hpp"

#include <cmath>
#include <filesystem>

#include "buddy_vision/emotion_model_retinaface.hpp"
#include "buddy_vision/emotion_model_haar.hpp"

VisionPipelineNode::VisionPipelineNode(const rclcpp::NodeOptions& options)
    : rclcpp_lifecycle::LifecycleNode(
          "vision", rclcpp::NodeOptions(options).automatically_declare_parameters_from_overrides(true)) {}

CallbackReturn VisionPipelineNode::on_configure(const rclcpp_lifecycle::State&) {
    RCLCPP_INFO(get_logger(), "VisionPipelineNode: configuring");

    // Engine selection: "auto" | "retinaface" | "haar"
    std::string engine = this->get_parameter_or("engine", std::string("auto"));
    std::string runtime = this->get_parameter_or("runtime", std::string("auto"));
    std::string face_model_dir = this->get_parameter_or("face_model_dir", std::string("models/face_emotion"));
    std::string haar_model_path = this->get_parameter_or("emotion_model_path", std::string("models/emotion"));

    bool use_retinaface = false;
    if (engine == "retinaface") {
        use_retinaface = true;
    } else if (engine == "auto") {
        namespace fs = std::filesystem;
        use_retinaface = fs::exists(fs::path(face_model_dir) / "retinaface_mnet_v2_fp16.onnx") ||
                         fs::exists(fs::path(face_model_dir) / "retinaface_mnet_v2_fp16.rknn");
    }

    if (use_retinaface) {
        auto model = std::make_unique<FaceEmotionModel>(get_logger(), runtime);
        if (model->load(face_model_dir)) {
            emotion_model_ = std::move(model);
            RCLCPP_INFO(get_logger(), "Using RetinaFace + AffectNet7 emotion pipeline");
        } else {
            RCLCPP_WARN(get_logger(), "RetinaFace load failed, falling back to Haar");
            use_retinaface = false;
        }
    }

    if (!use_retinaface) {
        emotion_model_ = std::make_unique<EmotionOnnxModel>(get_logger());
        if (!emotion_model_->load(haar_model_path)) {
            RCLCPP_ERROR(get_logger(), "Failed to load emotion model from %s", haar_model_path.c_str());
        }
    }

    // Publishers
    emotion_pub_ = create_publisher<buddy_interfaces::msg::EmotionResult>("/vision/emotion/result", 10);

    // Subscriptions (raw sensor_msgs/Image)
    rclcpp::SubscriptionOptions sub_opts;
    sub_opts.callback_group = create_callback_group(rclcpp::CallbackGroupType::MutuallyExclusive);

    emotion_sub_ = create_subscription<sensor_msgs::msg::Image>(
        "/camera_emotion/image_raw",
        rclcpp::SensorDataQoS(),
        [this](const sensor_msgs::msg::Image::ConstSharedPtr& msg) { on_emotion_frame(msg); },
        sub_opts);

    game_sub_ = create_subscription<sensor_msgs::msg::Image>(
        "/camera_game/image_raw",
        rclcpp::SensorDataQoS(),
        [this](const sensor_msgs::msg::Image::ConstSharedPtr& msg) { on_game_frame(msg); },
        sub_opts);

    // Capture services
    capture_srvs_["emotion"] = create_service<buddy_interfaces::srv::CaptureImage>(
        "/vision/emotion/capture",
        [this](const std::shared_ptr<buddy_interfaces::srv::CaptureImage::Request> req,
               std::shared_ptr<buddy_interfaces::srv::CaptureImage::Response> res) {
            handle_capture("emotion", req, res);
        });

    capture_srvs_["game"] = create_service<buddy_interfaces::srv::CaptureImage>(
        "/vision/game/capture",
        [this](const std::shared_ptr<buddy_interfaces::srv::CaptureImage::Request> req,
               std::shared_ptr<buddy_interfaces::srv::CaptureImage::Response> res) {
            handle_capture("game", req, res);
        });

    return CallbackReturn::SUCCESS;
}

CallbackReturn VisionPipelineNode::on_activate(const rclcpp_lifecycle::State&) {
    RCLCPP_INFO(get_logger(), "VisionPipelineNode: activating");

    int interval_ms = this->get_parameter_or("inference_interval_ms", 500);
    inference_timer_ = create_wall_timer(std::chrono::milliseconds(interval_ms), [this]() { do_inference(); });

    return CallbackReturn::SUCCESS;
}

CallbackReturn VisionPipelineNode::on_deactivate(const rclcpp_lifecycle::State&) {
    RCLCPP_INFO(get_logger(), "VisionPipelineNode: deactivating");
    inference_timer_.reset();
    return CallbackReturn::SUCCESS;
}

CallbackReturn VisionPipelineNode::on_cleanup(const rclcpp_lifecycle::State&) {
    RCLCPP_INFO(get_logger(), "VisionPipelineNode: cleaning up");
    emotion_sub_.reset();
    game_sub_.reset();
    emotion_pub_.reset();
    capture_srvs_.clear();
    inference_timer_.reset();
    emotion_model_.reset();
    latest_emotion_frame_.reset();
    latest_game_frame_.reset();
    camera_states_.clear();
    return CallbackReturn::SUCCESS;
}

CallbackReturn VisionPipelineNode::on_shutdown(const rclcpp_lifecycle::State&) {
    RCLCPP_INFO(get_logger(), "VisionPipelineNode: shutting down");
    return CallbackReturn::SUCCESS;
}

CallbackReturn VisionPipelineNode::on_error(const rclcpp_lifecycle::State&) {
    RCLCPP_ERROR(get_logger(), "VisionPipelineNode: error");
    return CallbackReturn::SUCCESS;
}

void VisionPipelineNode::on_emotion_frame(const sensor_msgs::msg::Image::ConstSharedPtr& msg) {
    latest_emotion_frame_ = msg;
    update_camera_state("emotion");
}

void VisionPipelineNode::on_game_frame(const sensor_msgs::msg::Image::ConstSharedPtr& msg) {
    latest_game_frame_ = msg;
    update_camera_state("game");
}

void VisionPipelineNode::update_camera_state(const std::string& name) {
    auto& st = camera_states_[name];
    if (!st.online) {
        RCLCPP_INFO(get_logger(), "[camera:%s] online", name.c_str());
        st.online = true;
    }
    st.last_frame_time = now();
}

void VisionPipelineNode::check_camera_timeouts() {
    const auto timeout = rclcpp::Duration::from_seconds(3.0);
    const auto t = now();
    for (auto& [name, st] : camera_states_) {
        if (st.online && (t - st.last_frame_time) > timeout) {
            RCLCPP_WARN(get_logger(),
                        "[camera:%s] offline (no frames for %.0fs)",
                        name.c_str(),
                        (t - st.last_frame_time).seconds());
            st.online = false;
        }
    }
}

void VisionPipelineNode::do_inference() {
    check_camera_timeouts();
    if (!latest_emotion_frame_ || !emotion_model_) {
        return;
    }

    auto cv_ptr = cv_bridge::toCvShare(latest_emotion_frame_, "bgr8");
    auto result = emotion_model_->inference(cv_ptr->image);

    if (result.label != last_label_ || std::abs(result.confidence - last_confidence_) > 0.05f) {
        RCLCPP_INFO(get_logger(), "[emotion] %s (%.2f)", result.label.c_str(), result.confidence);
        last_label_ = result.label;
        last_confidence_ = result.confidence;

        auto msg = buddy_interfaces::msg::EmotionResult();
        msg.emotion = result.label;
        msg.confidence = result.confidence;
        msg.timestamp = now();
        emotion_pub_->publish(msg);
    }
}

void VisionPipelineNode::handle_capture(const std::string& camera_name,
                                        const std::shared_ptr<buddy_interfaces::srv::CaptureImage::Request>,
                                        std::shared_ptr<buddy_interfaces::srv::CaptureImage::Response> response) {
    auto& frame = (camera_name == "emotion") ? latest_emotion_frame_ : latest_game_frame_;
    if (frame) {
        response->image = *frame;
    }
}

#include <rclcpp_components/register_node_macro.hpp>
RCLCPP_COMPONENTS_REGISTER_NODE(VisionPipelineNode)
