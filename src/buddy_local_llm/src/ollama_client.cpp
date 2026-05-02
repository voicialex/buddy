#include "buddy_local_llm/ollama_client.hpp"

#include <curl/curl.h>
#include <sstream>

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

struct StreamContext {
  std::string buffer;
  OllamaClient::ChunkCallback callback;
};

static size_t stream_callback(char *ptr, size_t size, size_t nmemb,
                              void *userdata) {
  auto *ctx = static_cast<StreamContext *>(userdata);
  ctx->buffer.append(ptr, size * nmemb);

  auto &buf = ctx->buffer;
  size_t pos = 0;
  while (pos < buf.size()) {
    auto nl = buf.find('\n', pos);
    if (nl == std::string::npos)
      break;

    std::string line = buf.substr(pos, nl - pos);
    pos = nl + 1;

    if (line.empty())
      continue;

    auto content_key = std::string(R"("content":")");
    auto cp = line.find(content_key);
    if (cp != std::string::npos) {
      cp += content_key.size();
      auto end = line.find('"', cp);
      if (end != std::string::npos) {
        auto text = line.substr(cp, end - cp);
        std::string unescaped;
        for (size_t i = 0; i < text.size(); ++i) {
          if (text[i] == '\\' && i + 1 < text.size() && text[i + 1] == 'n') {
            unescaped += '\n';
            ++i;
          } else {
            unescaped += text[i];
          }
        }
        ctx->callback(unescaped, false);
      }
    }

    auto done_key = std::string(R"("done":true)");
    if (line.find(done_key) != std::string::npos) {
      ctx->callback("", true);
    }
  }

  buf = buf.substr(pos);
  return size * nmemb;
}

OllamaClient::OllamaClient(const std::string &api_url, const std::string &model,
                           int timeout_seconds)
    : api_url_(api_url), model_(model), timeout_seconds_(timeout_seconds) {}

bool OllamaClient::chat_streaming(const std::vector<ChatMessage> &messages,
                                  const ChunkCallback &callback) {
  std::ostringstream body;
  body << R"({"model":")" << json_escape(model_) << R"(","messages":[)";

  for (size_t i = 0; i < messages.size(); ++i) {
    if (i > 0)
      body << ",";
    body << R"({"role":")" << json_escape(messages[i].role)
         << R"(","content":")" << json_escape(messages[i].content) << R"("})";
  }

  body << R"(],"stream":true})";

  std::string body_str = body.str();

  CURL *curl = curl_easy_init();
  if (!curl)
    return false;

  struct curl_slist *headers = nullptr;
  headers = curl_slist_append(headers, "Content-Type: application/json");

  StreamContext ctx;
  ctx.callback = callback;

  curl_easy_setopt(curl, CURLOPT_URL, (api_url_ + "/api/chat").c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, stream_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(timeout_seconds_));

  auto res = curl_easy_perform(curl);
  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);

  return res == CURLE_OK;
}
