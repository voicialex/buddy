#include "buddy_cloud/cloud_client_node.hpp"

#include <curl/curl.h>
#include <opencv2/opencv.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <sstream>

namespace trigger_types {
constexpr const char *kVoice = "voice";
constexpr const char *kEmotion = "emotion";
} // namespace trigger_types

static std::string json_escape(const std::string &s) {
  std::string out;
  out.reserve(s.size() * 2);
  for (unsigned char c : s) {
    switch (c) {
    case '"':
      out += "\\\"";
      break;
    case '\\':
      out += "\\\\";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    case '\b':
      out += "\\b";
      break;
    case '\f':
      out += "\\f";
      break;
    default:
      if (c < 0x20) {
        char buf[8];
        std::snprintf(buf, sizeof(buf), "\\u%04x", c);
        out += buf;
      } else {
        out += static_cast<char>(c);
      }
    }
  }
  return out;
}

static size_t write_callback(char *ptr, size_t size, size_t nmemb,
                             void *userdata) {
  auto *response = static_cast<std::string *>(userdata);
  response->append(ptr, size * nmemb);
  return size * nmemb;
}

CloudClientNode::CloudClientNode(const rclcpp::NodeOptions &options)
    : rclcpp_lifecycle::LifecycleNode("cloud", options) {}

CallbackReturn CloudClientNode::on_configure(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "CloudClientNode: configuring");

  declare_parameter("doubao.api_key", "");
  declare_parameter("doubao.model", "doubao-1.5-pro");
  declare_parameter(
      "doubao.endpoint",
      "https://ark.cn-beijing.volces.com/api/v3/chat/completions");
  declare_parameter("image_max_width", 512);
  declare_parameter("timeout_seconds", 30);

  api_key_ = get_parameter("doubao.api_key").as_string();
  model_ = get_parameter("doubao.model").as_string();
  endpoint_ = get_parameter("doubao.endpoint").as_string();
  image_max_width_ = get_parameter("image_max_width").as_int();
  timeout_seconds_ = get_parameter("timeout_seconds").as_int();

  const char *env_key = std::getenv("DOUBAO_API_KEY");
  if (env_key && env_key[0] != '\0') {
    api_key_ = env_key;
  }

  cloud_chunk_pub_ = create_publisher<buddy_interfaces::msg::InferenceChunk>(
      "/inference/cloud_chunk", 10);
  inference_request_sub_ =
      create_subscription<buddy_interfaces::msg::InferenceRequest>(
          "/brain/request", 10,
          std::bind(&CloudClientNode::on_inference_request, this,
                    std::placeholders::_1));

  curl_global_init(CURL_GLOBAL_DEFAULT);
  return CallbackReturn::SUCCESS;
}

CallbackReturn CloudClientNode::on_activate(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "CloudClientNode: activating");
  return CallbackReturn::SUCCESS;
}
CallbackReturn CloudClientNode::on_deactivate(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "CloudClientNode: deactivating");
  if (worker_thread_.joinable()) {
    worker_thread_.join();
  }
  return CallbackReturn::SUCCESS;
}
CallbackReturn CloudClientNode::on_cleanup(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "CloudClientNode: cleaning up");
  if (worker_thread_.joinable()) {
    worker_thread_.join();
  }
  cloud_chunk_pub_.reset();
  inference_request_sub_.reset();
  curl_global_cleanup();
  return CallbackReturn::SUCCESS;
}
CallbackReturn CloudClientNode::on_shutdown(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "CloudClientNode: shutting down");
  if (worker_thread_.joinable()) {
    worker_thread_.join();
  }
  return CallbackReturn::SUCCESS;
}
CallbackReturn CloudClientNode::on_error(const rclcpp_lifecycle::State &) {
  RCLCPP_ERROR(get_logger(), "CloudClientNode: error");
  return CallbackReturn::SUCCESS;
}

void CloudClientNode::on_inference_request(
    const buddy_interfaces::msg::InferenceRequest &msg) {
  RCLCPP_INFO(get_logger(), "Inference request [%s]: %s",
              msg.trigger_type.c_str(), msg.user_text.c_str());
  std::lock_guard<std::mutex> lock(request_mtx_);
  if (worker_thread_.joinable()) {
    RCLCPP_WARN(get_logger(), "Previous request still in progress, waiting");
    worker_thread_.join();
  }
  worker_thread_ = std::thread([this, msg]() { call_doubao(msg); });
}

std::string
CloudClientNode::encode_image_base64(const sensor_msgs::msg::Image &image,
                                     int max_width) {
  cv::Mat mat(image.height, image.width, CV_8UC3, (void *)image.data.data());
  if (image.encoding == "rgb8") {
    cv::cvtColor(mat, mat, cv::COLOR_RGB2BGR);
  }

  if (mat.cols > max_width) {
    double scale = static_cast<double>(max_width) / mat.cols;
    cv::resize(mat, mat, cv::Size(), scale, scale);
  }

  std::vector<uchar> buf;
  cv::imencode(".jpg", mat, buf);

  static const char table[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string b64;
  b64.reserve(((buf.size() + 2) / 3) * 4);
  for (size_t i = 0; i < buf.size(); i += 3) {
    uint32_t n = static_cast<uint32_t>(buf[i]) << 16;
    if (i + 1 < buf.size())
      n |= static_cast<uint32_t>(buf[i + 1]) << 8;
    if (i + 2 < buf.size())
      n |= static_cast<uint32_t>(buf[i + 2]);
    b64 += table[(n >> 18) & 0x3F];
    b64 += table[(n >> 12) & 0x3F];
    b64 += (i + 1 < buf.size()) ? table[(n >> 6) & 0x3F] : '=';
    b64 += (i + 2 < buf.size()) ? table[n & 0x3F] : '=';
  }
  return b64;
}

void CloudClientNode::call_doubao(
    const buddy_interfaces::msg::InferenceRequest &msg) {
  if (api_key_.empty()) {
    RCLCPP_ERROR(get_logger(), "No API key configured for Doubao");
    auto chunk = buddy_interfaces::msg::InferenceChunk();
    chunk.session_id = msg.session_id;
    chunk.turn_id = msg.turn_id;
    chunk.chunk_text = "API key not configured.";
    chunk.is_final = true;
    cloud_chunk_pub_->publish(chunk);
    return;
  }

  std::ostringstream messages;
  messages << "[";

  if (!msg.system_prompt.empty()) {
    messages << R"({"role":"system","content":")"
             << json_escape(msg.system_prompt) << R"("},)";
  }

  for (auto &h : msg.dialog_history) {
    auto colon = h.find(": ");
    if (colon != std::string::npos) {
      auto role = h.substr(0, colon);
      auto content = h.substr(colon + 2);
      messages << R"({"role":")" << json_escape(role) << R"(","content":")"
               << json_escape(content) << R"("},)";
    }
  }

  messages << R"({"role":"user","content":[)";

  std::string text_content = msg.user_text;
  if (text_content.empty() && msg.trigger_type == trigger_types::kEmotion) {
    text_content =
        "I notice you seem " + msg.emotion + ". How are you feeling?";
  }
  if (!msg.emotion.empty()) {
    text_content +=
        " [emotion: " + msg.emotion + " " +
        std::to_string(static_cast<int>(msg.emotion_confidence * 100)) + "%]";
  }
  messages << R"({"type":"text","text":")" << json_escape(text_content)
           << R"("})";

  if (!msg.image.data.empty()) {
    auto b64 = encode_image_base64(msg.image, image_max_width_);
    messages
        << R"(,{"type":"image_url","image_url":{"url":"data:image/jpeg;base64,)"
        << b64 << R"("}})";
  }

  messages << "]}]";

  std::ostringstream body;
  body << R"({"model":")" << model_ << R"(","messages":)" << messages.str()
       << "}";

  std::string body_str = body.str();

  CURL *curl = curl_easy_init();
  if (!curl) {
    RCLCPP_ERROR(get_logger(), "Failed to init curl");
    return;
  }

  std::string response;
  struct curl_slist *headers = nullptr;
  headers = curl_slist_append(headers, "Content-Type: application/json");
  std::string auth = "Authorization: Bearer " + api_key_;
  headers = curl_slist_append(headers, auth.c_str());

  curl_easy_setopt(curl, CURLOPT_URL, endpoint_.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(timeout_seconds_));

  auto res = curl_easy_perform(curl);
  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);

  auto chunk = buddy_interfaces::msg::InferenceChunk();
  chunk.session_id = msg.session_id;
  chunk.turn_id = msg.turn_id;

  if (res != CURLE_OK) {
    RCLCPP_ERROR(get_logger(), "Doubao API error: %s", curl_easy_strerror(res));
    chunk.chunk_text = "Cloud request failed.";
    chunk.is_final = true;
    cloud_chunk_pub_->publish(chunk);
    return;
  }

  auto content_key = std::string(R"("content":")");
  auto pos = response.rfind(content_key);
  if (pos != std::string::npos) {
    pos += content_key.size();
    auto end = response.find('"', pos);
    chunk.chunk_text = response.substr(pos, end - pos);
  } else {
    RCLCPP_WARN(get_logger(), "Unexpected Doubao response: %s",
                response.c_str());
    chunk.chunk_text = response;
  }

  chunk.is_final = true;
  cloud_chunk_pub_->publish(chunk);
  RCLCPP_INFO(get_logger(), "Doubao response: %s", chunk.chunk_text.c_str());
}

#include <rclcpp_components/register_node_macro.hpp>
RCLCPP_COMPONENTS_REGISTER_NODE(CloudClientNode)
