#include "buddy_common/http_client.hpp"

#include <curl/curl.h>

namespace buddy {

namespace {

size_t write_to_string(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* buf = static_cast<std::string*>(userdata);
    const size_t total = size * nmemb;
    buf->append(ptr, total);
    return total;
}

struct StreamCtxInternal {
    StreamCallback callback;
    bool aborted = false;
};

size_t stream_write(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* ctx = static_cast<StreamCtxInternal*>(userdata);
    const size_t total = size * nmemb;
    if (ctx->aborted) {
        return 0;  // abort transfer
    }
    if (!ctx->callback(ptr, total)) {
        ctx->aborted = true;
        return 0;
    }
    return total;
}

curl_slist* build_headers(const std::vector<std::string>& extra_headers) {
    curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    for (const auto& h : extra_headers) {
        headers = curl_slist_append(headers, h.c_str());
    }
    return headers;
}

}  // namespace

HttpResponse http_post_json(const std::string& url, const std::string& json_body,
                            const std::vector<std::string>& extra_headers, long timeout_sec) {
    HttpResponse response;

    CURL* curl = curl_easy_init();
    if (!curl) {
        response.error = "curl_easy_init failed";
        return response;
    }

    curl_slist* headers = build_headers(extra_headers);

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(json_body.size()));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_to_string);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_sec);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        response.error = curl_easy_strerror(res);
    } else {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status_code);
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return response;
}

long http_post_stream(const std::string& url, const std::string& json_body,
                      const std::vector<std::string>& extra_headers, StreamCallback on_chunk,
                      long timeout_sec) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        return 0;
    }

    curl_slist* headers = build_headers(extra_headers);
    StreamCtxInternal ctx{std::move(on_chunk), false};

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(json_body.size()));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, stream_write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_sec);

    CURLcode res = curl_easy_perform(curl);
    long status = 0;
    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return status;
}

}  // namespace buddy
