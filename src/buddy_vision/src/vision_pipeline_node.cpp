#include "buddy_vision/vision_pipeline_node.hpp"
#include "buddy_vision/onnx_emotion_model.hpp"

#include <ament_index_cpp/get_package_share_directory.hpp>
#include <fstream>
#include <set>

VisionPipelineNode::VisionPipelineNode(const rclcpp::NodeOptions &options)
    : rclcpp_lifecycle::LifecycleNode(
          "vision",
          rclcpp::NodeOptions(options)
              .automatically_declare_parameters_from_overrides(true)) {}

CallbackReturn
VisionPipelineNode::on_configure(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "VisionPipelineNode: configuring");

  auto camera_names = discover_camera_names();
  if (camera_names.empty()) {
    RCLCPP_WARN(get_logger(), "No camera configs found in parameters");
  }

  std::string pkg_path;
  try {
    pkg_path = ament_index_cpp::get_package_share_directory("buddy_vision");
  } catch (const std::exception &e) {
    RCLCPP_WARN(get_logger(), "Could not resolve buddy_vision package path: %s",
                e.what());
    pkg_path = "";
  }

  bool preview = this->get_parameter_or("preview", false);

  for (auto &name : camera_names) {
    CameraConfig cfg = load_camera_config(name);
    if (!pkg_path.empty()) {
      cfg.model_path = pkg_path + "/" + cfg.model_path;
    }

    // Always register topic and service so the ROS graph is complete
    result_pubs_[name] = create_publisher<buddy_interfaces::msg::EmotionResult>(
        "/vision/" + name + "/result", 10);

    capture_srvs_[name] = create_service<buddy_interfaces::srv::CaptureImage>(
        "/vision/" + name + "/capture",
        [this, n = name](
            const std::shared_ptr<buddy_interfaces::srv::CaptureImage::Request>
                req,
            std::shared_ptr<buddy_interfaces::srv::CaptureImage::Response>
                res) { handle_capture(n, req, res); });

    // Probe device existence; skip worker creation if not available
    if (!std::ifstream(cfg.device_path).good()) {
      RCLCPP_WARN(get_logger(),
                  "Camera [%s]: device %s not found, pending until connected",
                  name.c_str(), cfg.device_path.c_str());
      pending_configs_[name] = cfg;
      continue;
    }

    auto model = std::make_unique<EmotionOnnxModel>();
    workers_[name] = std::make_unique<CameraWorker>(cfg, get_logger(),
                                                    std::move(model), preview);

    workers_[name]->set_result_callback(
        [this, pub = result_pubs_[name], n = name, last_label = std::string{}](
            const std::string &label, float confidence) mutable {
          if (label != last_label) {
            RCLCPP_INFO(get_logger(), "[%s] %s (%.2f)", n.c_str(),
                        label.c_str(), confidence);
            last_label = label;
          }
          auto msg = buddy_interfaces::msg::EmotionResult();
          msg.emotion = label;
          msg.confidence = confidence;
          msg.timestamp = now();
          pub->publish(msg);
        });

    RCLCPP_INFO(get_logger(), "Configured camera: [%s] → %s", name.c_str(),
                cfg.device_path.c_str());
  }

  return CallbackReturn::SUCCESS;
}

CallbackReturn
VisionPipelineNode::on_activate(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "VisionPipelineNode: activating");
  for (auto &[name, worker] : workers_) {
    if (!worker->start()) {
      RCLCPP_WARN(get_logger(), "Camera [%s] failed to start, skipping",
                  name.c_str());
    }
  }
  for (auto &[name, cfg] : pending_configs_) {
    RCLCPP_WARN(get_logger(),
                "Camera [%s] still pending: device %s not available",
                name.c_str(), cfg.device_path.c_str());
  }
  return CallbackReturn::SUCCESS;
}

CallbackReturn
VisionPipelineNode::on_deactivate(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "VisionPipelineNode: deactivating");
  for (auto &[name, worker] : workers_) {
    worker->stop();
  }
  return CallbackReturn::SUCCESS;
}

CallbackReturn VisionPipelineNode::on_cleanup(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "VisionPipelineNode: cleaning up");
  workers_.clear();
  pending_configs_.clear();
  result_pubs_.clear();
  capture_srvs_.clear();
  return CallbackReturn::SUCCESS;
}

CallbackReturn
VisionPipelineNode::on_shutdown(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "VisionPipelineNode: shutting down");
  return CallbackReturn::SUCCESS;
}

CallbackReturn VisionPipelineNode::on_error(const rclcpp_lifecycle::State &) {
  RCLCPP_ERROR(get_logger(), "VisionPipelineNode: error");
  return CallbackReturn::SUCCESS;
}

std::vector<std::string> VisionPipelineNode::discover_camera_names() {
  // ROS 2 flattens nested YAML maps into dotted parameter names:
  //   cameras.emotion.device_path → parameter "cameras.emotion.device_path"
  // Use list_parameters with prefix "cameras" and depth 2 to get
  // "cameras.emotion.device_path", then extract "emotion".
  auto result = this->list_parameters({"cameras"}, 10);
  std::set<std::string> names;
  for (auto &p : result.names) {
    const std::string prefix = "cameras.";
    if (p.rfind(prefix, 0) != 0)
      continue;
    auto rest = p.substr(prefix.size());
    auto dot = rest.find('.');
    if (dot == std::string::npos)
      continue;
    names.insert(rest.substr(0, dot));
  }
  return {names.begin(), names.end()};
}

CameraConfig VisionPipelineNode::load_camera_config(const std::string &name) {
  auto prefix = "cameras." + name + ".";
  CameraConfig cfg;
  cfg.name = name;
  cfg.device_path = this->get_parameter(prefix + "device_path").as_string();
  cfg.frame_width = this->get_parameter(prefix + "frame_width").as_int();
  cfg.frame_height = this->get_parameter(prefix + "frame_height").as_int();
  cfg.model_path = this->get_parameter(prefix + "model_path").as_string();
  cfg.model_input_width =
      this->get_parameter(prefix + "model_input_width").as_int();
  cfg.model_input_height =
      this->get_parameter(prefix + "model_input_height").as_int();
  cfg.inference_interval_ms =
      this->get_parameter(prefix + "inference_interval_ms").as_int();
  return cfg;
}

void VisionPipelineNode::handle_capture(
    const std::string &camera_name,
    const std::shared_ptr<buddy_interfaces::srv::CaptureImage::Request>,
    std::shared_ptr<buddy_interfaces::srv::CaptureImage::Response> response) {
  auto it = workers_.find(camera_name);
  if (it == workers_.end()) {
    RCLCPP_WARN(get_logger(), "Capture requested for unknown camera: %s",
                camera_name.c_str());
    return;
  }

  cv::Mat frame;
  if (it->second->get_latest_frame(frame) && !frame.empty()) {
    auto &img = response->image;
    img.height = frame.rows;
    img.width = frame.cols;
    img.encoding = "bgr8";
    img.step = static_cast<uint32_t>(frame.cols * frame.elemSize());
    img.data.assign(frame.datastart, frame.dataend);
  }
}

#include <rclcpp_components/register_node_macro.hpp>
RCLCPP_COMPONENTS_REGISTER_NODE(VisionPipelineNode)
