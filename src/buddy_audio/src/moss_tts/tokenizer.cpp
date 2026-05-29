#include "buddy_audio/moss_tts/tokenizer.hpp"

#include <iostream>
#include <string>

#include <sentencepiece_processor.h>

namespace moss_tts {

struct Tokenizer::Impl {
    sentencepiece::SentencePieceProcessor processor;
};

Tokenizer::Tokenizer() = default;
Tokenizer::~Tokenizer() = default;

bool Tokenizer::load(const std::string& model_path) {
    impl_ = std::make_unique<Impl>();
    auto status = impl_->processor.Load(model_path);
    if (!status.ok()) {
        std::cerr << "[MOSS-TTS] Tokenizer load failed: " << model_path << " - " << status.ToString() << std::endl;
        impl_.reset();
        return false;
    }
    loaded_ = true;
    return true;
}

std::vector<int32_t> Tokenizer::encode(const std::string& text) const {
    if (!loaded_) {
        return {};
    }

    std::vector<std::string> pieces;
    impl_->processor.Encode(text, &pieces);

    auto is_cjk = [](char32_t cp) -> bool {
        return (cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0x3400 && cp <= 0x4DBF) || (cp >= 0x20000 && cp <= 0x2A6DF);
    };

    auto decode_utf8 = [](const std::string& value, size_t& pos) -> char32_t {
        if (pos >= value.size()) {
            return 0;
        }
        const unsigned char c0 = static_cast<unsigned char>(value[pos]);
        char32_t cp = 0;
        int len = 1;
        if (c0 < 0x80) {
            cp = c0;
        } else if (c0 < 0xE0 && pos + 1 < value.size()) {
            cp = ((c0 & 0x1F) << 6) | (static_cast<unsigned char>(value[pos + 1]) & 0x3F);
            len = 2;
        } else if (c0 < 0xF0 && pos + 2 < value.size()) {
            cp = ((c0 & 0x0F) << 12) | ((static_cast<unsigned char>(value[pos + 1]) & 0x3F) << 6) |
                 (static_cast<unsigned char>(value[pos + 2]) & 0x3F);
            len = 3;
        } else if (pos + 3 < value.size()) {
            cp = ((c0 & 0x07) << 18) | ((static_cast<unsigned char>(value[pos + 1]) & 0x3F) << 12) |
                 ((static_cast<unsigned char>(value[pos + 2]) & 0x3F) << 6) |
                 (static_cast<unsigned char>(value[pos + 3]) & 0x3F);
            len = 4;
        }
        pos += static_cast<size_t>(len);
        return cp;
    };

    auto strip_sp_marker = [](const std::string& value) -> std::string {
        if (value.size() >= 3 && static_cast<unsigned char>(value[0]) == 0xE2 &&
            static_cast<unsigned char>(value[1]) == 0x96 && static_cast<unsigned char>(value[2]) == 0x81) {
            return value.substr(3);
        }
        return value;
    };

    auto count_utf8_chars = [&](const std::string& value) -> int {
        int count = 0;
        size_t pos = 0;
        while (pos < value.size()) {
            decode_utf8(value, pos);
            ++count;
        }
        return count;
    };

    auto all_cjk = [&](const std::string& value) -> bool {
        size_t pos = 0;
        while (pos < value.size()) {
            if (!is_cjk(decode_utf8(value, pos))) {
                return false;
            }
        }
        return true;
    };

    std::vector<int32_t> result;
    for (const std::string& piece : pieces) {
        const std::string clean_piece = strip_sp_marker(piece);
        if (!clean_piece.empty() && count_utf8_chars(clean_piece) >= 2 && all_cjk(clean_piece)) {
            size_t pos = 0;
            bool split_ok = true;
            while (pos < clean_piece.size()) {
                const size_t start = pos;
                decode_utf8(clean_piece, pos);
                const std::string ch = clean_piece.substr(start, pos - start);
                const int token_id = impl_->processor.PieceToId(ch);
                if (token_id < 0) {
                    split_ok = false;
                    break;
                }
                result.push_back(static_cast<int32_t>(token_id));
            }
            if (!split_ok) {
                result.push_back(static_cast<int32_t>(impl_->processor.PieceToId(piece)));
            }
            continue;
        }

        const int token_id = impl_->processor.PieceToId(piece);
        if (token_id >= 0) {
            result.push_back(static_cast<int32_t>(token_id));
        }
    }
    return result;
}

}  // namespace moss_tts
