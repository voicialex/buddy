#pragma once
#include <functional>
#include <string>
#include <vector>

struct ChatMessage {
  std::string role;
  std::string content;
};

class OllamaClient {
public:
  using ChunkCallback =
      std::function<void(const std::string &chunk, bool done)>;

  OllamaClient(const std::string &api_url, const std::string &model,
               int timeout_seconds);

  bool chat_streaming(const std::vector<ChatMessage> &messages,
                      const ChunkCallback &callback);

private:
  std::string api_url_;
  std::string model_;
  int timeout_seconds_;
};
