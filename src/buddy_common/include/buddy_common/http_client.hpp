#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace buddy {

/// Simple HTTP response.
struct HttpResponse {
    long status_code = 0;
    std::string body;
    std::string error;
};

/// Callback for streaming responses. Return false to abort.
using StreamCallback = std::function<bool(const char* data, size_t len)>;

/// Perform a blocking HTTP POST with JSON body. Returns response.
HttpResponse http_post_json(const std::string& url, const std::string& json_body,
                            const std::vector<std::string>& extra_headers = {},
                            long timeout_sec = 30);

/// Perform a streaming HTTP POST. Calls `on_chunk` for each piece of data.
/// Returns final status code (0 on connection error).
long http_post_stream(const std::string& url, const std::string& json_body,
                      const std::vector<std::string>& extra_headers, StreamCallback on_chunk,
                      long timeout_sec = 120);

}  // namespace buddy
