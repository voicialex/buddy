#pragma once

#include <string>
#include <vector>

/// Splits streaming text into complete sentences at punctuation boundaries.
class SentenceSegmenter {
public:
    /// Feed a text chunk; returns any complete sentences found.
    std::vector<std::string> feed(const std::string& text);

    /// Flush remaining buffer content as a final sentence.
    std::string flush();

    void reset();

private:
    std::string buffer_;
};
