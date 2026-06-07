#include "buddy_audio/melo_rknn/backend.hpp"
#include "buddy_audio/melo_rknn/frontend.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>

namespace melo_tts {
namespace {

constexpr int64_t kMaxPhoneLen = 128;
constexpr int64_t kMaxBertTokens = 128;
constexpr int64_t kBertDim = 1024;
constexpr int64_t kJaBertDim = 768;
constexpr int64_t kZhMixEnLanguageId = 3;
constexpr int64_t kZhToneStart = 0;
constexpr int64_t kSampleRateFallback = 44100;
constexpr size_t kMinSegmentChars = 10;
constexpr size_t kMaxSegmentChars = 100;
constexpr int64_t kPadTokenId = 0;
constexpr int64_t kUnkTokenId = 100;
constexpr int64_t kClsTokenId = 101;
constexpr int64_t kSepTokenId = 102;
constexpr double kJiebaMinLogProb = -3.14e100;

struct WordPos {
    std::string word;
    std::string pos;
};

struct PreparedPhones {
    std::vector<std::string> phones;
    std::vector<int64_t> tones;
    std::vector<int64_t> word2ph;
    std::vector<std::string> bert_units;
    std::string normalized_text;
};

std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("failed to open resource: " + path.string());
    }
    std::ostringstream ss;
    ss << input.rdbuf();
    return ss.str();
}

std::vector<std::string> split_lines(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (!line.empty()) {
            lines.push_back(line);
        }
    }
    return lines;
}

std::vector<std::string> split_ws(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream in(text);
    std::string item;
    while (in >> item) {
        out.push_back(item);
    }
    return out;
}

std::vector<std::string> json_string_array(const std::string& json, const std::string& key) {
    const std::regex re("\"" + key + "\"\\s*:\\s*\\[([^\\]]*)\\]");
    std::smatch match;
    if (!std::regex_search(json, match, re)) {
        throw std::runtime_error("missing JSON string array key: " + key);
    }
    std::vector<std::string> values;
    const std::string body = match[1].str();
    const std::regex item_re("\"((?:[^\"\\\\]|\\\\.)*)\"");
    for (auto it = std::sregex_iterator(body.begin(), body.end(), item_re);
         it != std::sregex_iterator(); ++it) {
        values.push_back((*it)[1].str());
    }
    return values;
}

int json_int_value(const std::string& json, const std::string& key, int fallback) {
    const std::regex re("\"" + key + "\"\\s*:\\s*([0-9]+)");
    std::smatch match;
    if (std::regex_search(json, match, re)) {
        return std::stoi(match[1].str());
    }
    return fallback;
}

int json_speaker_id(const std::string& json, const std::string& speaker, int fallback) {
    if (speaker.empty()) {
        const std::regex any_re("\"spk2id\"\\s*:\\s*\\{[^}]*\"[^\"]+\"\\s*:\\s*([0-9]+)");
        std::smatch match;
        if (std::regex_search(json, match, any_re)) {
            return std::stoi(match[1].str());
        }
        return fallback;
    }
    const std::regex re("\"" + speaker + "\"\\s*:\\s*([0-9]+)");
    std::smatch match;
    if (!std::regex_search(json, match, re)) {
        throw std::runtime_error("unknown speaker in configuration.json: " + speaker);
    }
    return std::stoi(match[1].str());
}

std::vector<std::string> utf8_chars(const std::string& text) {
    std::vector<std::string> chars;
    for (size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        size_t len = 1;
        if ((c & 0xE0) == 0xC0) len = 2;
        else if ((c & 0xF0) == 0xE0) len = 3;
        else if ((c & 0xF8) == 0xF0) len = 4;
        if (i + len > text.size()) {
            break;
        }
        chars.push_back(text.substr(i, len));
        i += len;
    }
    return chars;
}

size_t utf8_char_count(const std::string& text) {
    return utf8_chars(text).size();
}

uint32_t utf8_codepoint(const std::string& s) {
    const unsigned char* p = reinterpret_cast<const unsigned char*>(s.data());
    if (s.size() == 1) return p[0];
    if (s.size() == 2) return ((p[0] & 0x1F) << 6) | (p[1] & 0x3F);
    if (s.size() == 3) return ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
    if (s.size() == 4) return ((p[0] & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
    return 0;
}

bool is_chinese(uint32_t cp) {
    return cp >= 0x4E00 && cp <= 0x9FFF;
}

bool is_ascii_alpha(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) != 0;
}

bool is_ascii_alnum_or_internal(char c) {
    const auto uc = static_cast<unsigned char>(c);
    return std::isalnum(uc) != 0 || c == '+' || c == '#' || c == '&' || c == '.' || c == '_';
}

bool is_ascii_number_token(const std::string& text) {
    if (text.empty()) return false;
    return std::all_of(text.begin(), text.end(), [](char c) {
        return std::isdigit(static_cast<unsigned char>(c)) != 0 || c == '.';
    });
}

bool is_punctuation_phone(const std::string& text) {
    static const std::unordered_set<std::string> punc = {"!", "?", "…", ",", ".", "'", "-", "¿", "¡"};
    return punc.count(text) != 0;
}

std::string join_chars(const std::vector<std::string>& chars, size_t begin, size_t end) {
    std::string out;
    for (size_t i = begin; i < end; ++i) {
        out += chars[i];
    }
    return out;
}

std::string normalize_punctuation_char(const std::string& ch) {
    if (ch == "，" || ch == "、" || ch == "：" || ch == "；" || ch == "·") return ",";
    if (ch == "。" || ch == "\n") return ".";
    if (ch == "！") return "!";
    if (ch == "？") return "?";
    if (ch == "“" || ch == "”" || ch == "‘" || ch == "’" || ch == "（" || ch == "）" ||
        ch == "(" || ch == ")" || ch == "《" || ch == "》" || ch == "【" || ch == "】" ||
        ch == "[" || ch == "]" || ch == "「" || ch == "」") return "'";
    if (ch == "—" || ch == "～" || ch == "~") return "-";
    if (ch == "…" || ch == "," || ch == "." || ch == "!" || ch == "?" || ch == "'" || ch == "-") return ch;
    return {};
}

std::string strip_tone_mark(const std::string& marked, int* tone_out) {
    static const std::unordered_map<std::string, std::pair<std::string, int>> table = {
        {"ā", {"a", 1}}, {"á", {"a", 2}}, {"ǎ", {"a", 3}}, {"à", {"a", 4}},
        {"ē", {"e", 1}}, {"é", {"e", 2}}, {"ě", {"e", 3}}, {"è", {"e", 4}},
        {"ī", {"i", 1}}, {"í", {"i", 2}}, {"ǐ", {"i", 3}}, {"ì", {"i", 4}},
        {"ō", {"o", 1}}, {"ó", {"o", 2}}, {"ǒ", {"o", 3}}, {"ò", {"o", 4}},
        {"ū", {"u", 1}}, {"ú", {"u", 2}}, {"ǔ", {"u", 3}}, {"ù", {"u", 4}},
        {"ǖ", {"v", 1}}, {"ǘ", {"v", 2}}, {"ǚ", {"v", 3}}, {"ǜ", {"v", 4}},
        {"ü", {"v", 5}},
    };
    std::string out;
    int tone = 5;
    for (const auto& ch : utf8_chars(marked)) {
        auto it = table.find(ch);
        if (it != table.end()) {
            out += it->second.first;
            tone = it->second.second;
        } else {
            out += ch;
        }
    }
    *tone_out = tone;
    return out;
}

std::string normalize_pinyin_for_opencpop(std::string pinyin) {
    static const std::unordered_map<std::string, std::string> single_rep = {
        {"ing", "ying"}, {"i", "yi"}, {"in", "yin"}, {"u", "wu"},
    };
    auto it = single_rep.find(pinyin);
    if (it != single_rep.end()) {
        return it->second;
    }
    if (!pinyin.empty()) {
        if (pinyin[0] == 'v') pinyin = "yu" + pinyin.substr(1);
        else if (pinyin[0] == 'i') pinyin = "y" + pinyin.substr(1);
        else if (pinyin[0] == 'u') pinyin = "w" + pinyin.substr(1);
    }
    return pinyin;
}

bool split_initial_final(const std::string& pinyin_without_mark, std::string* initial, std::string* final) {
    static const std::vector<std::string> initials = {
        "zh", "ch", "sh", "b", "p", "m", "f", "d", "t", "n", "l", "g", "k", "h",
        "j", "q", "x", "r", "z", "c", "s", "y", "w",
    };
    for (const auto& candidate : initials) {
        if (pinyin_without_mark.rfind(candidate, 0) == 0) {
            *initial = candidate;
            *final = pinyin_without_mark.substr(candidate.size());
            if (*initial == "y") {
                *initial = "";
                if (*final == "u") *final = "v";
                else if (final->rfind("u", 0) == 0) *final = "v" + final->substr(1);
                else if (final->rfind("i", 0) != 0) *final = "i" + *final;
            } else if (*initial == "w") {
                *initial = "";
                if (*final != "u" && final->rfind("u", 0) != 0) *final = "u" + *final;
            } else if ((*initial == "j" || *initial == "q" || *initial == "x") && final->rfind("u", 0) == 0) {
                *final = "v" + final->substr(1);
            }
            return true;
        }
    }
    initial->clear();
    *final = pinyin_without_mark;
    if (final->rfind("yu", 0) == 0) {
        *final = "v" + final->substr(2);
    } else if (final->rfind("y", 0) == 0) {
        *final = "i" + final->substr(1);
    } else if (final->rfind("w", 0) == 0) {
        *final = "u" + final->substr(1);
    }
    return !final->empty();
}

std::string opencpop_key_from_initial_final(const std::string& initial, std::string final_without_tone) {
    std::string pinyin = initial + final_without_tone;
    if (!initial.empty()) {
        static const std::unordered_map<std::string, std::string> v_rep = {
            {"uei", "ui"}, {"iou", "iu"}, {"uen", "un"},
        };
        auto it = v_rep.find(final_without_tone);
        if (it != v_rep.end()) {
            pinyin = initial + it->second;
        }
    } else {
        pinyin = normalize_pinyin_for_opencpop(final_without_tone);
    }
    return pinyin;
}

Tensor make_i64(std::vector<int64_t> shape, std::vector<int64_t> data) {
    Tensor t;
    t.dtype = DType::Int64;
    t.shape = std::move(shape);
    t.i64 = std::move(data);
    t.validate();
    return t;
}

Tensor make_f32(std::vector<int64_t> shape, std::vector<float> data) {
    Tensor t;
    t.dtype = DType::Float32;
    t.shape = std::move(shape);
    t.f32 = std::move(data);
    t.validate();
    return t;
}

std::vector<int64_t> intersperse_blank(const std::vector<int64_t>& input) {
    std::vector<int64_t> output(input.size() * 2 + 1, 0);
    for (size_t i = 0; i < input.size(); ++i) {
        output[i * 2 + 1] = input[i];
    }
    return output;
}

std::string ascii_lower(std::string value) {
    for (char& ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

void load_vocab_file(
    const std::filesystem::path& path,
    std::unordered_map<std::string, int64_t>* token_to_id) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("failed to open BERT vocab: " + path.string());
    }
    std::string line;
    int64_t id = 0;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (!line.empty()) {
            (*token_to_id)[line] = id;
        }
        ++id;
    }
}

int64_t token_id_or_unk(
    const std::unordered_map<std::string, int64_t>& token_to_id,
    const std::string& token) {
    auto it = token_to_id.find(token);
    if (it != token_to_id.end()) {
        return it->second;
    }
    it = token_to_id.find(ascii_lower(token));
    if (it != token_to_id.end()) {
        return it->second;
    }
    return kUnkTokenId;
}

std::unordered_map<std::string, Tensor> build_bert_inputs(
    const std::unordered_map<std::string, int64_t>& token_to_id,
    const std::vector<std::string>& bert_units) {
    std::vector<int64_t> input_ids(kMaxBertTokens, kPadTokenId);
    std::vector<int64_t> token_type_ids(kMaxBertTokens, 0);
    std::vector<int64_t> attention_mask(kMaxBertTokens, 0);

    std::vector<int64_t> ids;
    ids.reserve(bert_units.size() + 2);
    ids.push_back(kClsTokenId);
    for (const auto& unit : bert_units) {
        ids.push_back(token_id_or_unk(token_to_id, unit));
    }
    ids.push_back(kSepTokenId);
    if (ids.size() > static_cast<size_t>(kMaxBertTokens)) {
        throw std::runtime_error("segment is too long for static BERT shape: tokens=" + std::to_string(ids.size()));
    }
    for (size_t i = 0; i < ids.size(); ++i) {
        input_ids[i] = ids[i];
        attention_mask[i] = 1;
    }

    return {
        {"input_ids", make_i64({1, kMaxBertTokens}, std::move(input_ids))},
        {"token_type_ids", make_i64({1, kMaxBertTokens}, std::move(token_type_ids))},
        {"attention_mask", make_i64({1, kMaxBertTokens}, std::move(attention_mask))},
    };
}

std::vector<float> expand_bert_to_phone_level(
    const std::vector<float>& bert_tokens,
    const std::vector<int64_t>& word2ph,
    int64_t expected_phone_len) {
    const size_t expected_values = static_cast<size_t>(kMaxBertTokens * kJaBertDim);
    if (bert_tokens.size() < expected_values) {
        throw std::runtime_error(
            "BERT RKNN output is smaller than expected: values=" + std::to_string(bert_tokens.size()));
    }
    std::vector<float> ja_bert(static_cast<size_t>(kJaBertDim * kMaxPhoneLen), 0.0f);
    int64_t phone_pos = 0;
    for (size_t token = 0; token < word2ph.size(); ++token) {
        const int64_t repeat = word2ph[token];
        for (int64_t r = 0; r < repeat; ++r) {
            if (phone_pos >= kMaxPhoneLen) {
                throw std::runtime_error("expanded BERT feature exceeds static phone shape");
            }
            for (int64_t dim = 0; dim < kJaBertDim; ++dim) {
                ja_bert[static_cast<size_t>(dim * kMaxPhoneLen + phone_pos)] =
                    bert_tokens[static_cast<size_t>(token * kJaBertDim + dim)];
            }
            ++phone_pos;
        }
    }
    if (phone_pos != expected_phone_len) {
        throw std::runtime_error(
            "expanded BERT length does not match phone length: bert=" + std::to_string(phone_pos) +
            " phones=" + std::to_string(expected_phone_len));
    }
    return ja_bert;
}

void load_jieba_dict(
    const std::filesystem::path& path,
    std::unordered_map<std::string, int64_t>* freq,
    std::unordered_map<std::string, std::string>* pos,
    int64_t* total) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("failed to open jieba dict: " + path.string());
    }
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        const auto first = line.find(' ');
        const auto second = first == std::string::npos ? std::string::npos : line.find(' ', first + 1);
        if (first == std::string::npos || second == std::string::npos) continue;
        const std::string word = line.substr(0, first);
        const auto value = static_cast<int64_t>(std::stoll(line.substr(first + 1, second - first - 1)));
        (*freq)[word] = value;
        (*pos)[word] = line.substr(second + 1);
        *total += value;
        const auto chars = utf8_chars(word);
        std::string prefix;
        for (size_t i = 0; i + 1 < chars.size(); ++i) {
            prefix += chars[i];
            if (freq->find(prefix) == freq->end()) {
                (*freq)[prefix] = 0;
            }
        }
    }
}

void extract_python_set(
    const std::string& source,
    const std::string& name,
    std::unordered_set<std::string>* out) {
    const auto key = "self." + name;
    auto pos = source.find(key);
    if (pos == std::string::npos) return;
    pos = source.find('{', pos);
    if (pos == std::string::npos) return;
    const auto end = source.find('}', pos);
    if (end == std::string::npos) return;
    const std::string body = source.substr(pos, end - pos);
    const std::regex item_re("\"([^\"]+)\"");
    for (auto it = std::sregex_iterator(body.begin(), body.end(), item_re);
         it != std::sregex_iterator(); ++it) {
        out->insert((*it)[1].str());
    }
}

std::unordered_map<std::string, std::vector<std::string>> load_phrase_pinyin_json(
    const std::filesystem::path& path) {
    const std::string json = read_text_file(path);
    std::unordered_map<std::string, std::vector<std::string>> result;
    size_t i = 0;
    auto skip_ws = [&]() {
        while (i < json.size() && std::isspace(static_cast<unsigned char>(json[i]))) ++i;
    };
    auto parse_string = [&]() -> std::string {
        if (i >= json.size() || json[i] != '"') return {};
        ++i;
        std::string out;
        while (i < json.size()) {
            const char c = json[i++];
            if (c == '"') break;
            if (c == '\\' && i < json.size()) {
                out += json[i++];
            } else {
                out += c;
            }
        }
        return out;
    };
    skip_ws();
    if (i < json.size() && json[i] == '{') ++i;
    while (i < json.size()) {
        skip_ws();
        if (i >= json.size() || json[i] == '}') break;
        const std::string phrase = parse_string();
        skip_ws();
        if (i < json.size() && json[i] == ':') ++i;
        skip_ws();
        std::vector<std::string> pinyins;
        if (i < json.size() && json[i] == '[') {
            int depth = 0;
            do {
                if (json[i] == '[') {
                    ++depth;
                    ++i;
                    continue;
                }
                if (json[i] == ']') {
                    --depth;
                    ++i;
                    continue;
                }
                if (json[i] == '"' && depth == 2) {
                    pinyins.push_back(parse_string());
                    continue;
                }
                ++i;
            } while (i < json.size() && depth > 0);
        }
        if (!phrase.empty() && !pinyins.empty()) {
            result[phrase] = std::move(pinyins);
        }
        skip_ws();
        if (i < json.size() && json[i] == ',') ++i;
    }
    return result;
}

std::string marked_pinyin_to_final_tone(const std::string& marked, std::string* initial) {
    int tone = 5;
    const std::string plain = strip_tone_mark(marked, &tone);
    std::string final;
    split_initial_final(plain, initial, &final);
    return final + std::to_string(tone);
}

bool all_tone_three(const std::vector<std::string>& finals) {
    return !finals.empty() && std::all_of(finals.begin(), finals.end(), [](const std::string& item) {
        return !item.empty() && item.back() == '3';
    });
}

bool is_reduplication_word(const std::string& word) {
    const auto chars = utf8_chars(word);
    return chars.size() == 2 && chars[0] == chars[1];
}

std::vector<std::string> split_word_for_sandhi(
    const std::string& word,
    const std::unordered_map<std::string, int64_t>& jieba_freq) {
    const auto chars = utf8_chars(word);
    if (chars.size() <= 1) return {word, ""};
    std::vector<std::string> candidates;
    for (size_t n = 2; n <= 3 && n <= chars.size(); ++n) {
        for (size_t i = 0; i + n <= chars.size(); ++i) {
            const auto gram = join_chars(chars, i, i + n);
            auto it = jieba_freq.find(gram);
            if (it != jieba_freq.end() && it->second > 0) {
                candidates.push_back(gram);
            }
        }
    }
    if (candidates.empty()) {
        return {join_chars(chars, 0, 1), join_chars(chars, 1, chars.size())};
    }
    std::sort(candidates.begin(), candidates.end(), [](const auto& lhs, const auto& rhs) {
        const auto l = utf8_chars(lhs).size();
        const auto r = utf8_chars(rhs).size();
        if (l != r) return l < r;
        return lhs < rhs;
    });
    const auto first = candidates.front();
    const auto first_chars = utf8_chars(first);
    for (size_t i = 0; i + first_chars.size() <= chars.size(); ++i) {
        if (join_chars(chars, i, i + first_chars.size()) == first) {
            if (i == 0) {
                return {first, join_chars(chars, first_chars.size(), chars.size())};
            }
            return {join_chars(chars, 0, chars.size() - first_chars.size()), first};
        }
    }
    return {join_chars(chars, 0, 1), join_chars(chars, 1, chars.size())};
}

std::vector<WordPos> jieba_lcut_known(
    const std::string& sentence,
    const std::unordered_map<std::string, int64_t>& freq,
    const std::unordered_map<std::string, std::string>& pos,
    int64_t total) {
    const auto chars = utf8_chars(sentence);
    std::vector<WordPos> out;
    std::vector<std::string> block;
    auto flush_block = [&]() {
        if (block.empty()) return;
        const int n = static_cast<int>(block.size());
        std::vector<std::vector<int>> dag(n);
        for (int k = 0; k < n; ++k) {
            std::string frag;
            for (int i = k; i < n; ++i) {
                frag += block[static_cast<size_t>(i)];
                if (freq.find(frag) == freq.end()) break;
                auto fit = freq.find(frag);
                if (fit != freq.end() && fit->second > 0) {
                    dag[static_cast<size_t>(k)].push_back(i);
                }
            }
            if (dag[static_cast<size_t>(k)].empty()) dag[static_cast<size_t>(k)].push_back(k);
        }
        std::vector<double> score(static_cast<size_t>(n + 1), 0.0);
        std::vector<int> route(static_cast<size_t>(n + 1), n);
        const double log_total = std::log(static_cast<double>(std::max<int64_t>(total, 1)));
        for (int idx = n - 1; idx >= 0; --idx) {
            double best = -std::numeric_limits<double>::infinity();
            int best_x = idx;
            for (int x : dag[static_cast<size_t>(idx)]) {
                const std::string word = join_chars(block, static_cast<size_t>(idx), static_cast<size_t>(x + 1));
                auto fit = freq.find(word);
                const double word_freq = static_cast<double>((fit != freq.end() && fit->second > 0) ? fit->second : 1);
                const double candidate = std::log(word_freq) - log_total + score[static_cast<size_t>(x + 1)];
                if (candidate > best) {
                    best = candidate;
                    best_x = x;
                }
            }
            score[static_cast<size_t>(idx)] = best == -std::numeric_limits<double>::infinity() ? kJiebaMinLogProb : best;
            route[static_cast<size_t>(idx)] = best_x;
        }
        std::string buf;
        for (int x = 0; x < n;) {
            const int y = route[static_cast<size_t>(x)] + 1;
            const std::string word = join_chars(block, static_cast<size_t>(x), static_cast<size_t>(y));
            if (y - x == 1) {
                buf += word;
            } else {
                if (!buf.empty()) {
                    if (utf8_chars(buf).size() == 1 || freq.find(buf) != freq.end()) {
                        for (const auto& ch : utf8_chars(buf)) {
                            auto pit = pos.find(ch);
                            out.push_back({ch, pit == pos.end() ? "x" : pit->second});
                        }
                    } else {
                        for (const auto& ch : utf8_chars(buf)) {
                            auto pit = pos.find(ch);
                            out.push_back({ch, pit == pos.end() ? "x" : pit->second});
                        }
                    }
                    buf.clear();
                }
                auto pit = pos.find(word);
                out.push_back({word, pit == pos.end() ? "x" : pit->second});
            }
            x = y;
        }
        if (!buf.empty()) {
            for (const auto& ch : utf8_chars(buf)) {
                auto pit = pos.find(ch);
                out.push_back({ch, pit == pos.end() ? "x" : pit->second});
            }
        }
        block.clear();
    };

    std::string ascii;
    auto flush_ascii = [&]() {
        if (!ascii.empty()) {
            out.push_back({ascii, is_ascii_number_token(ascii) ? "m" : "eng"});
            ascii.clear();
        }
    };

    for (const auto& ch : chars) {
        const uint32_t cp = utf8_codepoint(ch);
        if (is_chinese(cp)) {
            flush_ascii();
            block.push_back(ch);
        } else if (ch.size() == 1 && is_ascii_alnum_or_internal(ch[0])) {
            flush_block();
            ascii += ch;
        } else {
            flush_ascii();
            flush_block();
            if (!std::isspace(static_cast<unsigned char>(ch[0]))) {
                out.push_back({ch, "x"});
            }
        }
    }
    flush_ascii();
    flush_block();
    return out;
}

std::vector<WordPos> merge_bu(std::vector<WordPos> seg) {
    std::vector<WordPos> out;
    std::string last_word;
    for (auto item : seg) {
        if (last_word == "不") {
            item.word = last_word + item.word;
        }
        if (item.word != "不") {
            out.push_back(item);
        }
        last_word = item.word;
    }
    if (last_word == "不") {
        out.push_back({"不", "d"});
    }
    return out;
}

std::vector<WordPos> merge_yi(const std::vector<WordPos>& seg) {
    std::vector<WordPos> out;
    for (size_t i = 0; i < seg.size(); ++i) {
        const auto& item = seg[i];
        if (i >= 1 && item.word == "一" && i + 1 < seg.size() &&
            seg[i - 1].word == seg[i + 1].word && seg[i - 1].pos == "v") {
            if (!out.empty()) {
                out.back().word = out.back().word + "一" + out.back().word;
            }
        } else {
            if (i >= 2 && seg[i - 1].word == "一" && seg[i - 2].word == item.word && item.pos == "v") {
                continue;
            }
            out.push_back(item);
        }
    }
    std::vector<WordPos> merged;
    for (auto item : out) {
        if (!merged.empty() && merged.back().word == "一") {
            merged.back().word += item.word;
        } else {
            merged.push_back(item);
        }
    }
    return merged;
}

std::vector<WordPos> merge_reduplication(const std::vector<WordPos>& seg) {
    std::vector<WordPos> out;
    for (const auto& item : seg) {
        if (!out.empty() && item.word == out.back().word) {
            out.back().word += item.word;
        } else {
            out.push_back(item);
        }
    }
    return out;
}

std::vector<WordPos> merge_er(const std::vector<WordPos>& seg) {
    std::vector<WordPos> out;
    for (const auto& item : seg) {
        if (!out.empty() && item.word == "儿" && out.back().word != "#") {
            out.back().word += item.word;
        } else {
            out.push_back(item);
        }
    }
    return out;
}

std::vector<std::string> finals_for_word(
    const std::string& word,
    const std::unordered_map<uint32_t, std::string>& codepoint_to_pinyin,
    const std::unordered_map<std::string, std::vector<std::string>>& phrase_to_pinyin = {}) {
    std::vector<std::string> finals;
    const auto chars = utf8_chars(word);
    auto phrase_it = phrase_to_pinyin.find(word);
    if (phrase_it != phrase_to_pinyin.end() && phrase_it->second.size() == chars.size()) {
        for (const auto& marked : phrase_it->second) {
            std::string initial;
            finals.push_back(marked_pinyin_to_final_tone(marked, &initial));
        }
        return finals;
    }
    for (const auto& ch : chars) {
        const auto punct = normalize_punctuation_char(ch);
        if (!punct.empty()) {
            finals.push_back(punct);
            continue;
        }
        const uint32_t cp = utf8_codepoint(ch);
        auto it = codepoint_to_pinyin.find(cp);
        if (it == codepoint_to_pinyin.end()) {
            finals.push_back("5");
            continue;
        }
        int tone = 5;
        const std::string plain = strip_tone_mark(it->second, &tone);
        std::string initial;
        std::string final;
        split_initial_final(plain, &initial, &final);
        finals.push_back(final + std::to_string(tone));
    }
    return finals;
}

std::vector<WordPos> merge_continuous_three_tones(
    const std::vector<WordPos>& seg,
    const std::unordered_map<uint32_t, std::string>& codepoint_to_pinyin,
    const std::unordered_map<std::string, std::vector<std::string>>& phrase_to_pinyin,
    bool boundary_only) {
    std::vector<WordPos> out;
    std::vector<std::vector<std::string>> finals;
    finals.reserve(seg.size());
    for (const auto& item : seg) {
        finals.push_back(finals_for_word(item.word, codepoint_to_pinyin, phrase_to_pinyin));
    }
    std::vector<bool> merge_last(seg.size(), false);
    for (size_t i = 0; i < seg.size(); ++i) {
        bool should_merge = false;
        if (i >= 1 && !merge_last[i - 1] && !finals[i - 1].empty() && !finals[i].empty()) {
            if (!boundary_only) {
                should_merge = all_tone_three(finals[i - 1]) && all_tone_three(finals[i]);
            } else {
                should_merge = finals[i - 1].back().back() == '3' && finals[i].front().back() == '3';
            }
        }
        if (should_merge && !is_reduplication_word(seg[i - 1].word) &&
            utf8_chars(seg[i - 1].word).size() + utf8_chars(seg[i].word).size() <= 3) {
            if (!out.empty()) {
                out.back().word += seg[i].word;
                merge_last[i] = true;
            }
        } else {
            out.push_back(seg[i]);
        }
    }
    return out;
}

std::vector<WordPos> pre_merge_for_modify(
    std::vector<WordPos> seg,
    const std::unordered_map<uint32_t, std::string>& codepoint_to_pinyin,
    const std::unordered_map<std::string, std::vector<std::string>>& phrase_to_pinyin) {
    seg = merge_bu(std::move(seg));
    seg = merge_yi(seg);
    seg = merge_reduplication(seg);
    seg = merge_continuous_three_tones(seg, codepoint_to_pinyin, phrase_to_pinyin, false);
    seg = merge_continuous_three_tones(seg, codepoint_to_pinyin, phrase_to_pinyin, true);
    seg = merge_er(seg);
    return seg;
}

void apply_bu_sandhi(const std::string& word, std::vector<std::string>* finals) {
    const auto chars = utf8_chars(word);
    if (chars.size() == 3 && chars[1] == "不") {
        (*finals)[1].back() = '5';
        return;
    }
    for (size_t i = 0; i < chars.size(); ++i) {
        if (chars[i] == "不" && i + 1 < chars.size() && !(*finals)[i + 1].empty() && (*finals)[i + 1].back() == '4') {
            (*finals)[i].back() = '2';
        }
    }
}

void apply_yi_sandhi(const std::string& word, std::vector<std::string>* finals) {
    const auto chars = utf8_chars(word);
    bool has_yi = false;
    bool others_numeric = true;
    for (const auto& ch : chars) {
        if (ch == "一") has_yi = true;
        else if (!(ch.size() == 1 && std::isdigit(static_cast<unsigned char>(ch[0])))) others_numeric = false;
    }
    if (has_yi && others_numeric) return;
    if (chars.size() == 3 && chars[1] == "一" && chars[0] == chars[2]) {
        (*finals)[1].back() = '5';
    } else if (word.rfind("第一", 0) == 0 && finals->size() > 1) {
        (*finals)[1].back() = '1';
    } else {
        for (size_t i = 0; i < chars.size(); ++i) {
            if (chars[i] == "一" && i + 1 < chars.size()) {
                if (!(*finals)[i + 1].empty() && (*finals)[i + 1].back() == '4') {
                    (*finals)[i].back() = '2';
                } else if (!is_punctuation_phone(chars[i + 1])) {
                    (*finals)[i].back() = '4';
                }
            }
        }
    }
}

void apply_neural_sandhi(
    const std::string& word,
    const std::string& pos,
    const std::unordered_set<std::string>& must_neutral,
    const std::unordered_set<std::string>& must_not_neutral,
    const std::unordered_map<std::string, int64_t>& jieba_freq,
    std::vector<std::string>* finals) {
    const auto chars = utf8_chars(word);
    for (size_t j = 1; j < chars.size(); ++j) {
        if (chars[j] == chars[j - 1] && !pos.empty() &&
            (pos[0] == 'n' || pos[0] == 'v' || pos[0] == 'a') &&
            must_not_neutral.count(word) == 0) {
            (*finals)[j].back() = '5';
        }
    }
    const auto ge = std::find(chars.begin(), chars.end(), "个");
    const int ge_idx = ge == chars.end() ? -1 : static_cast<int>(std::distance(chars.begin(), ge));
    if (!chars.empty() && std::string("吧呢啊呐噻嘛吖嗨哦哒额滴哩哟喽啰耶喔诶").find(chars.back()) != std::string::npos) {
        finals->back().back() = '5';
    } else if (!chars.empty() && std::string("的地得").find(chars.back()) != std::string::npos) {
        finals->back().back() = '5';
    } else if (chars.size() > 1 && (chars.back() == "们" || chars.back() == "子") &&
               (pos == "r" || pos == "n") && must_not_neutral.count(word) == 0) {
        finals->back().back() = '5';
    } else if (chars.size() > 1 && (chars.back() == "上" || chars.back() == "下" || chars.back() == "里") &&
               (pos == "s" || pos == "l" || pos == "f")) {
        finals->back().back() = '5';
    } else if (chars.size() > 1 && (chars.back() == "来" || chars.back() == "去") &&
               std::string("上下进出回过起开").find(chars[chars.size() - 2]) != std::string::npos) {
        finals->back().back() = '5';
    } else if ((ge_idx >= 1 &&
                (chars[static_cast<size_t>(ge_idx - 1)].size() == 1 ||
                 std::string("几有两半多各整每做是").find(chars[static_cast<size_t>(ge_idx - 1)]) != std::string::npos)) ||
               word == "个") {
        if (ge_idx >= 0) (*finals)[static_cast<size_t>(ge_idx)].back() = '5';
    } else if (must_neutral.count(word) != 0 ||
               (chars.size() >= 2 && must_neutral.count(join_chars(chars, chars.size() - 2, chars.size())) != 0)) {
        finals->back().back() = '5';
    }

    const auto split = split_word_for_sandhi(word, jieba_freq);
    size_t offset = 0;
    for (const auto& sub : split) {
        const auto sub_chars = utf8_chars(sub);
        if (!sub_chars.empty() &&
            (must_neutral.count(sub) != 0 ||
             (sub_chars.size() >= 2 && must_neutral.count(join_chars(sub_chars, sub_chars.size() - 2, sub_chars.size())) != 0))) {
            const size_t idx = offset + sub_chars.size() - 1;
            if (idx < finals->size()) (*finals)[idx].back() = '5';
        }
        offset += sub_chars.size();
    }
}

void apply_three_sandhi(
    const std::string& word,
    const std::unordered_map<std::string, int64_t>& jieba_freq,
    std::vector<std::string>* finals) {
    const auto chars = utf8_chars(word);
    if (chars.size() == 2 && all_tone_three(*finals)) {
        (*finals)[0].back() = '2';
    } else if (chars.size() == 3) {
        const auto split = split_word_for_sandhi(word, jieba_freq);
        const auto first_len = utf8_chars(split[0]).size();
        if (all_tone_three(*finals)) {
            if (first_len == 2) {
                (*finals)[0].back() = '2';
                (*finals)[1].back() = '2';
            } else if (first_len == 1 && finals->size() > 1) {
                (*finals)[1].back() = '2';
            }
        } else if (first_len > 0 && first_len < finals->size()) {
            std::vector<std::string> first(finals->begin(), finals->begin() + static_cast<long>(first_len));
            std::vector<std::string> second(finals->begin() + static_cast<long>(first_len), finals->end());
            if (all_tone_three(first) && first.size() == 2) {
                (*finals)[0].back() = '2';
            } else if (!all_tone_three(second) && !second.empty() && second[0].back() == '3' && first.back().back() == '3') {
                (*finals)[first_len - 1].back() = '2';
            }
        }
    } else if (chars.size() == 4) {
        for (size_t start : {size_t{0}, size_t{2}}) {
            std::vector<std::string> sub = {(*finals)[start], (*finals)[start + 1]};
            if (all_tone_three(sub)) {
                (*finals)[start].back() = '2';
            }
        }
    }
}

std::vector<std::string> modified_tone(
    const std::string& word,
    const std::string& pos,
    std::vector<std::string> finals,
    const std::unordered_set<std::string>& must_neutral,
    const std::unordered_set<std::string>& must_not_neutral,
    const std::unordered_map<std::string, int64_t>& jieba_freq) {
    apply_bu_sandhi(word, &finals);
    apply_yi_sandhi(word, &finals);
    apply_neural_sandhi(word, pos, must_neutral, must_not_neutral, jieba_freq, &finals);
    apply_three_sandhi(word, jieba_freq, &finals);
    return finals;
}

bool append_phone_from_initial_final(
    const std::string& initial,
    const std::string& final_with_tone,
    const std::unordered_map<std::string, std::vector<std::string>>& pinyin_to_symbols,
    std::vector<std::string>* phones,
    std::vector<int64_t>* tones,
    std::vector<int64_t>* word2ph) {
    if (final_with_tone.empty()) return false;
    const char tone_ch = final_with_tone.back();
    if (tone_ch < '1' || tone_ch > '5') return false;
    const std::string final_without_tone = final_with_tone.substr(0, final_with_tone.size() - 1);
    const std::string key = opencpop_key_from_initial_final(initial, final_without_tone);
    auto sit = pinyin_to_symbols.find(key);
    if (sit == pinyin_to_symbols.end()) {
        return false;
    }
    word2ph->push_back(static_cast<int64_t>(sit->second.size()));
    for (const auto& ph : sit->second) {
        phones->push_back(ph);
        tones->push_back(static_cast<int64_t>(tone_ch - '0'));
    }
    return true;
}

PreparedPhones build_char_level_phones(
    const std::string& segment,
    const std::unordered_map<uint32_t, std::string>& codepoint_to_pinyin,
    const std::unordered_map<std::string, std::vector<std::string>>& pinyin_to_symbols) {
    PreparedPhones prepared;
    prepared.phones = {"_"};
    prepared.tones = {0};
    prepared.word2ph = {1};

    for (const auto& ch : utf8_chars(segment)) {
        const auto punct = normalize_punctuation_char(ch);
        if (!punct.empty()) {
            prepared.phones.push_back(punct);
            prepared.tones.push_back(0);
            prepared.word2ph.push_back(1);
            prepared.bert_units.push_back(punct);
            prepared.normalized_text += punct;
            continue;
        }
        const uint32_t cp = utf8_codepoint(ch);
        if (!is_chinese(cp)) {
            continue;
        }
        auto pit = codepoint_to_pinyin.find(cp);
        if (pit == codepoint_to_pinyin.end()) {
            continue;
        }
        int tone = 5;
        const std::string plain = strip_tone_mark(pit->second, &tone);
        std::string initial;
        std::string final;
        if (!split_initial_final(plain, &initial, &final)) {
            continue;
        }
        if (!append_phone_from_initial_final(
                initial,
                final + std::to_string(tone),
                pinyin_to_symbols,
                &prepared.phones,
                &prepared.tones,
                &prepared.word2ph)) {
            continue;
        }
        prepared.bert_units.push_back(ch);
        prepared.normalized_text += ch;
    }

    prepared.phones.push_back("_");
    prepared.tones.push_back(0);
    prepared.word2ph.push_back(1);
    return prepared;
}

PreparedPhones build_jieba_level_phones(
    const std::string& segment,
    const std::unordered_map<uint32_t, std::string>& codepoint_to_pinyin,
    const std::unordered_map<std::string, std::vector<std::string>>& phrase_to_pinyin,
    const std::unordered_map<std::string, std::vector<std::string>>& pinyin_to_symbols,
    const std::unordered_map<std::string, int64_t>& jieba_freq,
    const std::unordered_map<std::string, std::string>& jieba_pos,
    int64_t jieba_total,
    const std::unordered_set<std::string>& must_neutral,
    const std::unordered_set<std::string>& must_not_neutral) {
    PreparedPhones prepared;
    prepared.phones = {"_"};
    prepared.tones = {0};
    prepared.word2ph = {1};

    auto seg_cut = jieba_lcut_known(segment, jieba_freq, jieba_pos, jieba_total);
    seg_cut = pre_merge_for_modify(std::move(seg_cut), codepoint_to_pinyin, phrase_to_pinyin);

    for (const auto& item : seg_cut) {
        if (item.pos == "eng") {
            continue;
        }
        std::vector<std::string> initials;
        std::vector<std::string> finals;
        std::vector<std::string> chars_for_bert;
        const auto chars = utf8_chars(item.word);
        auto phrase_it = phrase_to_pinyin.find(item.word);
        if (phrase_it != phrase_to_pinyin.end() && phrase_it->second.size() == chars.size()) {
            for (size_t i = 0; i < chars.size(); ++i) {
                const auto& ch = chars[i];
                const auto punct = normalize_punctuation_char(ch);
                if (!punct.empty()) {
                    initials.push_back(punct);
                    finals.push_back(punct);
                    chars_for_bert.push_back(punct);
                    continue;
                }
                std::string initial;
                const std::string final = marked_pinyin_to_final_tone(phrase_it->second[i], &initial);
                initials.push_back(initial);
                finals.push_back(final);
                chars_for_bert.push_back(ch);
            }
        } else for (const auto& ch : chars) {
            const auto punct = normalize_punctuation_char(ch);
            if (!punct.empty()) {
                initials.push_back(punct);
                finals.push_back(punct);
                chars_for_bert.push_back(punct);
                continue;
            }
            const uint32_t cp = utf8_codepoint(ch);
            if (!is_chinese(cp)) {
                continue;
            }
            auto pit = codepoint_to_pinyin.find(cp);
            if (pit == codepoint_to_pinyin.end()) {
                continue;
            }
            int tone = 5;
            const std::string plain = strip_tone_mark(pit->second, &tone);
            std::string initial;
            std::string final;
            if (!split_initial_final(plain, &initial, &final)) {
                continue;
            }
            initials.push_back(initial);
            finals.push_back(final + std::to_string(tone));
            chars_for_bert.push_back(ch);
        }
        if (initials.size() != finals.size() || finals.empty()) {
            continue;
        }
        finals = modified_tone(
            item.word,
            item.pos,
            finals,
            must_neutral,
            must_not_neutral,
            jieba_freq);

        for (size_t i = 0; i < finals.size(); ++i) {
            if (is_punctuation_phone(finals[i])) {
                prepared.phones.push_back(finals[i]);
                prepared.tones.push_back(0);
                prepared.word2ph.push_back(1);
                prepared.bert_units.push_back(finals[i]);
                prepared.normalized_text += finals[i];
                continue;
            }
            if (append_phone_from_initial_final(
                    initials[i],
                    finals[i],
                    pinyin_to_symbols,
                    &prepared.phones,
                    &prepared.tones,
                    &prepared.word2ph)) {
                prepared.bert_units.push_back(chars_for_bert[i]);
                prepared.normalized_text += chars_for_bert[i];
            }
        }
    }

    prepared.phones.push_back("_");
    prepared.tones.push_back(0);
    prepared.word2ph.push_back(1);
    return prepared;
}

}  // namespace

MeloFrontend::MeloFrontend(FrontendOptions options) : options_(std::move(options)) {
    const std::filesystem::path root(options_.resource_dir);
    const std::string config = read_text_file(root / "checkpoint" / "rknn" / "configuration.json");
    sample_rate_ = json_int_value(config, "sampling_rate", kSampleRateFallback);
    speaker_id_ = json_speaker_id(config, options_.speaker, 1);

    const auto symbols = json_string_array(config, "symbols");
    for (size_t i = 0; i < symbols.size(); ++i) {
        symbol_to_id_[symbols[i]] = static_cast<int64_t>(i);
    }

    const auto opencpop = read_text_file(root / "model" / "MeloTTS-ONNX" / "melo_onnx" / "text" / "opencpop-strict.txt");
    for (const auto& line : split_lines(opencpop)) {
        const auto tab = line.find('\t');
        if (tab == std::string::npos) continue;
        pinyin_to_symbols_[line.substr(0, tab)] = split_ws(line.substr(tab + 1));
    }

    const std::filesystem::path pinyin_json = root / "third_party_data" / "pypinyin" / "pinyin_dict.json";
    const std::string pinyin_data = read_text_file(pinyin_json);
    const std::regex item_re("\"([0-9]+)\"\\s*:\\s*\"([^\"]+)\"");
    for (auto it = std::sregex_iterator(pinyin_data.begin(), pinyin_data.end(), item_re);
         it != std::sregex_iterator(); ++it) {
        const auto cp = static_cast<uint32_t>(std::stoul((*it)[1].str()));
        std::string value = (*it)[2].str();
        const auto comma = value.find(',');
        if (comma != std::string::npos) {
            value = value.substr(0, comma);
        }
        codepoint_to_pinyin_[cp] = value;
    }

    if (options_.bert_mode == "rknn") {
        const std::filesystem::path vocab_path = root / "checkpoint" / "rknn" / "vocab.txt";
        load_vocab_file(vocab_path, &token_to_id_);
    }

    if (options_.rank == "rank3") {
        load_jieba_dict(root / "third_party_data" / "jieba" / "dict.txt", &jieba_freq_, &jieba_pos_, &jieba_total_);
        phrase_to_pinyin_ = load_phrase_pinyin_json(root / "third_party_data" / "pypinyin" / "phrases_dict.json");
        const auto tone_source = read_text_file(root / "model" / "MeloTTS-ONNX" / "melo_onnx" / "text" / "tone_sandhi.py");
        extract_python_set(tone_source, "must_neural_tone_words", &must_neutral_tone_words_);
        extract_python_set(tone_source, "must_not_neural_tone_words", &must_not_neutral_tone_words_);
    }
}

std::vector<FrontendSegment> MeloFrontend::prepare(const std::string& text) const {
    std::unique_ptr<InferBackend> bert_backend;
    if (options_.bert_mode == "rknn") {
        std::string model_path = options_.bert_model_path;
        if (model_path.empty()) {
            model_path = options_.resource_dir + "/checkpoint/rknn/bert_lml_model.rknn";
        }
        bert_backend = create_backend();
        bert_backend->load_model(model_path);
    } else if (options_.bert_mode != "zero") {
        throw std::runtime_error("--bert-mode must be zero or rknn");
    }

    std::vector<std::string> segments_text;
    std::string current;
    for (const auto& ch : utf8_chars(text)) {
        if (ch == "嗯") {
            current += "恩";
            continue;
        }
        if (ch == "呣") {
            current += "母";
            continue;
        }
        const auto punct = normalize_punctuation_char(ch);
        if (!punct.empty()) {
            current += punct;
            const bool hard_break = punct == "." || punct == "!" || punct == "?";
            const bool long_comma_break = punct == "," && utf8_char_count(current) >= kMaxSegmentChars;
            if (hard_break || long_comma_break) {
                if (!current.empty()) {
                    segments_text.push_back(current);
                    current.clear();
                }
            }
            continue;
        }
        const uint32_t cp = utf8_codepoint(ch);
        if (is_chinese(cp)) {
            current += ch;
        } else if (ch.size() == 1 && is_ascii_alpha(ch[0])) {
            current += static_cast<char>(std::tolower(static_cast<unsigned char>(ch[0])));
        }
        if (utf8_char_count(current) >= kMaxSegmentChars) {
            segments_text.push_back(current);
            current.clear();
        }
    }
    if (!current.empty()) {
        if (!segments_text.empty() && utf8_char_count(current) <= kMinSegmentChars) {
            segments_text.back() += " " + current;
        } else {
            segments_text.push_back(current);
        }
    }
    if (segments_text.empty()) {
        throw std::runtime_error("frontend produced no text segments");
    }

    std::vector<FrontendSegment> segments;
    for (const auto& segment : segments_text) {
        PreparedPhones prepared = options_.rank == "rank3"
            ? build_jieba_level_phones(
                segment,
                codepoint_to_pinyin_,
                phrase_to_pinyin_,
                pinyin_to_symbols_,
                jieba_freq_,
                jieba_pos_,
                jieba_total_,
                must_neutral_tone_words_,
                must_not_neutral_tone_words_)
            : build_char_level_phones(segment, codepoint_to_pinyin_, pinyin_to_symbols_);

        std::vector<int64_t> phone_ids;
        phone_ids.reserve(prepared.phones.size());
        for (const auto& phone : prepared.phones) {
            auto it = symbol_to_id_.find(phone);
            if (it == symbol_to_id_.end()) {
                throw std::runtime_error("symbol not in MeloTTS config: " + phone);
            }
            phone_ids.push_back(it->second);
        }

        std::vector<int64_t> language_ids(prepared.phones.size(), kZhMixEnLanguageId);
        phone_ids = intersperse_blank(phone_ids);
        prepared.tones = intersperse_blank(prepared.tones);
        language_ids = intersperse_blank(language_ids);
        if (options_.bert_mode == "rknn") {
            for (auto& count : prepared.word2ph) {
                count *= 2;
            }
            prepared.word2ph[0] += 1;
        }

        if (static_cast<int64_t>(phone_ids.size()) > kMaxPhoneLen) {
            throw std::runtime_error("segment is too long for static RKNN shape: phones=" + std::to_string(phone_ids.size()));
        }
        if (options_.bert_mode == "rknn" && prepared.word2ph.size() != prepared.bert_units.size() + 2) {
            throw std::runtime_error("BERT token count and word2ph count are not aligned");
        }

        std::vector<float> ja_bert(kJaBertDim * kMaxPhoneLen, 0.0f);
        const int64_t phone_len = static_cast<int64_t>(phone_ids.size());
        if (bert_backend) {
            auto bert_inputs = build_bert_inputs(token_to_id_, prepared.bert_units);
            const auto bert_outputs = bert_backend->run(bert_inputs, {"bert_output"});
            ja_bert = expand_bert_to_phone_level(bert_outputs.at("bert_output").f32, prepared.word2ph, phone_len);
        }

        std::vector<int64_t> x(kMaxPhoneLen, 0);
        std::vector<int64_t> tone(kMaxPhoneLen, 0);
        std::vector<int64_t> language(kMaxPhoneLen, 0);
        for (size_t i = 0; i < phone_ids.size(); ++i) {
            x[i] = phone_ids[i];
            tone[i] = prepared.tones[i] + kZhToneStart;
            language[i] = language_ids[i];
        }

        FrontendSegment out;
        out.normalized_text = prepared.normalized_text;
        out.tensors = {
            {"x", make_i64({1, kMaxPhoneLen}, std::move(x))},
            {"x_lengths", make_i64({1}, {phone_len})},
            {"sid", make_i64({1}, {speaker_id_})},
            {"tone", make_i64({1, kMaxPhoneLen}, std::move(tone))},
            {"language", make_i64({1, kMaxPhoneLen}, std::move(language))},
            {"bert", make_f32({1, kBertDim, kMaxPhoneLen}, std::vector<float>(kBertDim * kMaxPhoneLen, 0.0f))},
            {"ja_bert", make_f32({1, kJaBertDim, kMaxPhoneLen}, std::move(ja_bert))},
            {"noise_scale", make_f32({1}, {options_.noise_scale})},
            {"length_scale", make_f32({1}, {1.0f / options_.speed})},
            {"noise_scale_w", make_f32({1}, {options_.noise_scale_w})},
            {"sdp_ratio", make_f32({1}, {options_.sdp_ratio})},
        };
        segments.push_back(std::move(out));
    }
    return segments;
}

}  // namespace melo_tts
