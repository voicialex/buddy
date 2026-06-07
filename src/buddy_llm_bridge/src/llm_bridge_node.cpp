#include "buddy_llm_bridge/llm_bridge_node.hpp"

#include <curl/curl.h>
#include <opencv2/imgcodecs.hpp>

#include <sstream>

using CallbackReturn = InferenceServerBase::CallbackReturn;

/// Base64 encode binary data
static std::string base64_encode(const std::vector<uint8_t>& data) {
    static constexpr char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);
    size_t i = 0;
    for (; i + 2 < data.size(); i += 3) {
        uint32_t n = (static_cast<uint32_t>(data[i]) << 16) | (static_cast<uint32_t>(data[i + 1]) << 8) | data[i + 2];
        out.push_back(table[(n >> 18) & 0x3F]);
        out.push_back(table[(n >> 12) & 0x3F]);
        out.push_back(table[(n >> 6) & 0x3F]);
        out.push_back(table[n & 0x3F]);
    }
    if (i < data.size()) {
        uint32_t n = static_cast<uint32_t>(data[i]) << 16;
        if (i + 1 < data.size()) n |= static_cast<uint32_t>(data[i + 1]) << 8;
        out.push_back(table[(n >> 18) & 0x3F]);
        out.push_back(table[(n >> 12) & 0x3F]);
        out.push_back(i + 1 < data.size() ? table[(n >> 6) & 0x3F] : '=');
        out.push_back('=');
    }
    return out;
}

/// SSE parsing context
struct SseCtx {
    std::shared_ptr<LlmBridgeNode::GoalHandle> goal_handle;
    std::string buffer;
    std::string full_response;
    std::string source;
    std::string model;
    std::atomic<bool>* cancel;
};

/// Extract string value for a key from simple JSON: {"key":"value",...}
static std::string json_extract_string(const std::string& json, const std::string& key) {
    auto search = "\"" + key + "\":\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) return {};
    pos += search.size();
    std::string result;
    while (pos < json.size()) {
        if (json[pos] == '\\' && pos + 1 < json.size()) {
            switch (json[pos + 1]) {
                case '"': result += '"'; break;
                case '\\': result += '\\'; break;
                case 'n': result += '\n'; break;
                default: result += json[pos + 1]; break;
            }
            pos += 2;
        } else if (json[pos] == '"') {
            break;
        } else {
            result += json[pos++];
        }
    }
    return result;
}

static bool json_has_true(const std::string& json, const std::string& key) {
    return json.find("\"" + key + "\":true") != std::string::npos;
}

static size_t sse_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* ctx = static_cast<SseCtx*>(userdata);
    if (ctx->cancel->load()) return 0;

    size_t bytes = size * nmemb;
    ctx->buffer.append(ptr, bytes);

    size_t last = 0;
    while (last < ctx->buffer.size()) {
        auto nl = ctx->buffer.find('\n', last);
        if (nl == std::string::npos) break;
        std::string line = ctx->buffer.substr(last, nl - last);
        last = nl + 1;

        if (line.rfind("data: ", 0) != 0) continue;
        auto json_str = line.substr(6);

        auto text = json_extract_string(json_str, "text");
        auto source = json_extract_string(json_str, "source");
        auto model = json_extract_string(json_str, "model");
        bool done = json_has_true(json_str, "done");

        if (!source.empty()) ctx->source = source;
        if (!model.empty()) ctx->model = model;

        if (!text.empty()) {
            ctx->full_response += text;
            auto feedback = std::make_shared<LlmBridgeNode::Inference::Feedback>();
            feedback->chunk_text = text;
            ctx->goal_handle->publish_feedback(feedback);
        }

        if (done) break;
    }
    ctx->buffer.erase(0, last);
    return bytes;
}

static std::string json_escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            default: out += c;
        }
    }
    return out;
}

static bool is_valid_llm_mode(const std::string& mode) {
    return mode == "local_only" || mode == "cloud_only" || mode == "local_route";
}

LlmBridgeNode::LlmBridgeNode(const rclcpp::NodeOptions& options)
    : InferenceServerBase("llm_bridge", "/inference/llm", options) {}

CallbackReturn LlmBridgeNode::on_configure(const rclcpp_lifecycle::State&) {
    RCLCPP_INFO(get_logger(), "LlmBridgeNode: configuring");

    declare_parameter("server_url", "http://127.0.0.1:8002");
    declare_parameter("mode", "local_route");

    server_url_ = get_parameter("server_url").as_string();
    mode_ = get_parameter("mode").as_string();
    if (!is_valid_llm_mode(mode_)) {
        RCLCPP_ERROR(get_logger(),
                     "Invalid llm_bridge.mode='%s'. Allowed: local_only | cloud_only | local_route",
                     mode_.c_str());
        return CallbackReturn::ERROR;
    }

    curl_global_init(CURL_GLOBAL_DEFAULT);
    create_action_server();
    return CallbackReturn::SUCCESS;
}

void LlmBridgeNode::execute(std::shared_ptr<GoalHandle> goal_handle) {
    const auto goal = goal_handle->get_goal();
    RCLCPP_INFO(get_logger(), "LLM request [%s]: %s", mode_.c_str(), goal->user_text.c_str());

    // Build JSON body
    std::ostringstream body;
    body << R"({"messages":[)";
    for (auto& h : goal->dialog_history) {
        auto colon = h.find(": ");
        if (colon != std::string::npos) {
            body << R"({"role":")" << json_escape(h.substr(0, colon))
                 << R"(","content":")" << json_escape(h.substr(colon + 2)) << R"("},)";
        }
    }
    std::string user_text = goal->user_text;
    if (user_text.empty()) user_text = "你好";
    body << R"({"role":"user","content":")" << json_escape(user_text) << R"("}],"mode":")"
         << mode_ << R"(","session_id":")" << json_escape(goal->session_id)
         << R"(","stream":true)";

    // Forward emotion text
    if (!goal->emotion.empty()) {
        body << R"(,"emotion":")" << json_escape(goal->emotion) << R"(")";
    }

    // Compress image to JPEG and base64 encode
    if (!goal->image.data.empty() && goal->image.height > 0 && goal->image.width > 0) {
        cv::Mat img(goal->image.height, goal->image.width, CV_8UC3,
                    const_cast<uint8_t*>(goal->image.data.data()));
        std::vector<uint8_t> jpeg_buf;
        if (cv::imencode(".jpg", img, jpeg_buf, {cv::IMWRITE_JPEG_QUALITY, 80})) {
            body << R"(,"image_base64":")" << base64_encode(jpeg_buf) << R"(")";
        }
    }

    body << "}";

    std::string body_str = body.str();
    std::string url = server_url_ + "/v1/chat";

    CURL* curl = curl_easy_init();
    if (!curl) {
        auto result = std::make_shared<Inference::Result>();
        result->success = false;
        result->error_message = "curl init failed";
        goal_handle->abort(result);
        return;
    }

    SseCtx ctx;
    ctx.goal_handle = goal_handle;
    ctx.cancel = &cancel_requested_;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, sse_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);

    auto res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    auto result = std::make_shared<Inference::Result>();
    if (cancel_requested_.load()) {
        result->full_response = ctx.full_response;
        result->success = false;
        result->error_message = "canceled";
        goal_handle->canceled(result);
    } else if (res != CURLE_OK) {
        result->success = false;
        result->error_message = curl_easy_strerror(res);
        goal_handle->abort(result);
        RCLCPP_WARN(get_logger(), "LLM server error: %s", curl_easy_strerror(res));
    } else {
        result->full_response = ctx.full_response;
        result->success = true;
        goal_handle->succeed(result);
        RCLCPP_INFO(get_logger(), "LLM response (%zu chars, source=%s, model=%s)",
                    ctx.full_response.size(), ctx.source.c_str(), ctx.model.c_str());
    }
}

#include <rclcpp_components/register_node_macro.hpp>
RCLCPP_COMPONENTS_REGISTER_NODE(LlmBridgeNode)
