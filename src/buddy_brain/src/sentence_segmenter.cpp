#include "buddy_brain/sentence_segmenter.hpp"

#include <algorithm>

std::vector<std::string> SentenceSegmenter::feed(const std::string& text) {
    buffer_ += text;
    std::vector<std::string> result;

    static const std::vector<std::string> delimiters = {"\xe3\x80\x82",  // 。
                                                        "\xef\xbc\x81",  // ！
                                                        "\xef\xbc\x9f",  // ？
                                                        ".",
                                                        "!",
                                                        "?"};

    size_t last = 0;
    while (last < buffer_.size()) {
        size_t best = std::string::npos;
        for (const auto& d : delimiters) {
            auto found = buffer_.find(d, last);
            if (found != std::string::npos && (best == std::string::npos || found < best)) {
                best = found + d.size();
            }
        }
        if (best == std::string::npos) {
            break;
        }
        result.push_back(buffer_.substr(last, best - last));
        last = best;
    }

    buffer_ = (last < buffer_.size()) ? buffer_.substr(last) : "";
    return result;
}

std::string SentenceSegmenter::flush() {
    std::string remainder = buffer_;
    buffer_.clear();
    return remainder;
}

void SentenceSegmenter::reset() {
    buffer_.clear();
}
