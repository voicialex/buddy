#include "buddy_audio/moss_tts/moss_tts_pipeline.hpp"

#include "buddy_audio/moss_tts/backend.hpp"
#include "buddy_audio/moss_tts/tokenizer.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <numeric>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace moss_tts {

namespace fs = std::filesystem;

namespace {

struct JsonValue;
using JsonArray = std::vector<JsonValue>;
using JsonObject = std::map<std::string, JsonValue>;

struct JsonValue {
    std::variant<std::nullptr_t, bool, double, std::string, JsonArray, JsonObject> value;

    bool is_object() const { return std::holds_alternative<JsonObject>(value); }
    bool is_array() const { return std::holds_alternative<JsonArray>(value); }
    bool is_string() const { return std::holds_alternative<std::string>(value); }
    bool is_number() const { return std::holds_alternative<double>(value); }

    const JsonObject& as_object() const { return std::get<JsonObject>(value); }
    const JsonArray& as_array() const { return std::get<JsonArray>(value); }
    const std::string& as_string() const { return std::get<std::string>(value); }
    double as_number() const { return std::get<double>(value); }
};

class JsonParser {
public:
    explicit JsonParser(std::string input) : input_(std::move(input)) {}

    JsonValue parse() {
        skip_ws();
        JsonValue result = parse_value();
        skip_ws();
        if (pos_ != input_.size()) {
            throw std::runtime_error("Unexpected trailing JSON input.");
        }
        return result;
    }

private:
    JsonValue parse_value() {
        skip_ws();
        if (pos_ >= input_.size()) {
            throw std::runtime_error("Unexpected end of JSON.");
        }
        const char c = input_[pos_];
        if (c == '{') {
            return JsonValue{parse_object()};
        }
        if (c == '[') {
            return JsonValue{parse_array()};
        }
        if (c == '"') {
            return JsonValue{parse_string()};
        }
        if (c == 't') {
            expect("true");
            return JsonValue{true};
        }
        if (c == 'f') {
            expect("false");
            return JsonValue{false};
        }
        if (c == 'n') {
            expect("null");
            return JsonValue{nullptr};
        }
        return JsonValue{parse_number()};
    }

    JsonObject parse_object() {
        JsonObject object;
        expect_char('{');
        skip_ws();
        if (peek('}')) {
            expect_char('}');
            return object;
        }
        while (true) {
            skip_ws();
            const std::string key = parse_string();
            skip_ws();
            expect_char(':');
            object.emplace(key, parse_value());
            skip_ws();
            if (peek('}')) {
                expect_char('}');
                break;
            }
            expect_char(',');
        }
        return object;
    }

    JsonArray parse_array() {
        JsonArray array;
        expect_char('[');
        skip_ws();
        if (peek(']')) {
            expect_char(']');
            return array;
        }
        while (true) {
            array.push_back(parse_value());
            skip_ws();
            if (peek(']')) {
                expect_char(']');
                break;
            }
            expect_char(',');
        }
        return array;
    }

    std::string parse_string() {
        expect_char('"');
        std::string result;
        while (pos_ < input_.size()) {
            const char c = input_[pos_++];
            if (c == '"') {
                return result;
            }
            if (c == '\\') {
                if (pos_ >= input_.size()) {
                    throw std::runtime_error("Invalid JSON escape.");
                }
                const char esc = input_[pos_++];
                switch (esc) {
                    case '"':
                    case '\\':
                    case '/':
                        result.push_back(esc);
                        break;
                    case 'b':
                        result.push_back('\b');
                        break;
                    case 'f':
                        result.push_back('\f');
                        break;
                    case 'n':
                        result.push_back('\n');
                        break;
                    case 'r':
                        result.push_back('\r');
                        break;
                    case 't':
                        result.push_back('\t');
                        break;
                    case 'u': {
                        if (pos_ + 4 > input_.size()) {
                            throw std::runtime_error("Invalid JSON unicode escape.");
                        }
                        const uint32_t codepoint = static_cast<uint32_t>(std::stoul(input_.substr(pos_, 4), nullptr, 16));
                        pos_ += 4;
                        if (codepoint <= 0x7F) {
                            result.push_back(static_cast<char>(codepoint));
                        } else if (codepoint <= 0x7FF) {
                            result.push_back(static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F)));
                            result.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
                        } else {
                            result.push_back(static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F)));
                            result.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
                            result.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
                        }
                        break;
                    }
                    default:
                        throw std::runtime_error("Unsupported JSON escape.");
                }
                continue;
            }
            result.push_back(c);
        }
        throw std::runtime_error("Unterminated JSON string.");
    }

    double parse_number() {
        const size_t start = pos_;
        if (input_[pos_] == '-') {
            ++pos_;
        }
        while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_]))) {
            ++pos_;
        }
        if (pos_ < input_.size() && input_[pos_] == '.') {
            ++pos_;
            while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_]))) {
                ++pos_;
            }
        }
        if (pos_ < input_.size() && (input_[pos_] == 'e' || input_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < input_.size() && (input_[pos_] == '+' || input_[pos_] == '-')) {
                ++pos_;
            }
            while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_]))) {
                ++pos_;
            }
        }
        return std::stod(input_.substr(start, pos_ - start));
    }

    void skip_ws() {
        while (pos_ < input_.size() && std::isspace(static_cast<unsigned char>(input_[pos_]))) {
            ++pos_;
        }
    }

    bool peek(char expected) const {
        return pos_ < input_.size() && input_[pos_] == expected;
    }

    void expect(const char* token) {
        const std::string expected(token);
        if (input_.substr(pos_, expected.size()) != expected) {
            throw std::runtime_error("Unexpected JSON token.");
        }
        pos_ += expected.size();
    }

    void expect_char(char expected) {
        skip_ws();
        if (pos_ >= input_.size() || input_[pos_] != expected) {
            throw std::runtime_error(std::string("Expected JSON character: ") + expected);
        }
        ++pos_;
    }

    std::string input_;
    size_t pos_ = 0;
};

const JsonValue& object_field(const JsonObject& object, const std::string& key) {
    const auto found = object.find(key);
    if (found == object.end()) {
        throw std::runtime_error("Missing JSON field: " + key);
    }
    return found->second;
}

std::string as_string(const JsonValue& value) {
    if (!value.is_string()) {
        throw std::runtime_error("Expected JSON string.");
    }
    return value.as_string();
}

int as_int(const JsonValue& value) {
    if (!value.is_number()) {
        throw std::runtime_error("Expected JSON number.");
    }
    return static_cast<int>(std::llround(value.as_number()));
}

std::vector<std::string> as_string_array(const JsonValue& value) {
    if (!value.is_array()) {
        throw std::runtime_error("Expected JSON array.");
    }
    std::vector<std::string> result;
    for (const JsonValue& item : value.as_array()) {
        result.push_back(as_string(item));
    }
    return result;
}

std::vector<int32_t> as_i32_array(const JsonValue& value) {
    if (!value.is_array()) {
        throw std::runtime_error("Expected JSON array.");
    }
    std::vector<int32_t> result;
    for (const JsonValue& item : value.as_array()) {
        result.push_back(static_cast<int32_t>(as_int(item)));
    }
    return result;
}

JsonValue parse_json_file(const fs::path& path) {
    std::ifstream input(path);
    if (!input.is_open()) {
        throw std::runtime_error("Failed to open JSON file: " + path.string());
    }
    const std::string content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    return JsonParser(content).parse();
}

struct WavData {
    int sample_rate = 0;
    int channels = 0;
    std::vector<float> interleaved;
};

uint16_t read_u16(std::istream& input) {
    uint8_t bytes[2];
    input.read(reinterpret_cast<char*>(bytes), 2);
    return static_cast<uint16_t>(bytes[0] | (static_cast<uint16_t>(bytes[1]) << 8));
}

uint32_t read_u32(std::istream& input) {
    uint8_t bytes[4];
    input.read(reinterpret_cast<char*>(bytes), 4);
    return static_cast<uint32_t>(bytes[0] | (static_cast<uint32_t>(bytes[1]) << 8) |
                                 (static_cast<uint32_t>(bytes[2]) << 16) | (static_cast<uint32_t>(bytes[3]) << 24));
}

WavData load_wav(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        throw std::runtime_error("Failed to open WAV file: " + path.string());
    }

    char riff[4];
    char wave[4];
    input.read(riff, 4);
    (void)read_u32(input);
    input.read(wave, 4);
    if (std::string(riff, 4) != "RIFF" || std::string(wave, 4) != "WAVE") {
        throw std::runtime_error("Unsupported WAV header: " + path.string());
    }

    uint16_t audio_format = 0;
    uint16_t bits_per_sample = 0;
    uint16_t channels = 0;
    uint32_t sample_rate = 0;
    std::vector<uint8_t> data_chunk;

    while (input.good() && !input.eof()) {
        char chunk_id_raw[4];
        input.read(chunk_id_raw, 4);
        if (input.gcount() != 4) {
            break;
        }
        const std::string chunk_id(chunk_id_raw, 4);
        const uint32_t chunk_size = read_u32(input);
        if (chunk_id == "fmt ") {
            audio_format = read_u16(input);
            channels = read_u16(input);
            sample_rate = read_u32(input);
            (void)read_u32(input);
            (void)read_u16(input);
            bits_per_sample = read_u16(input);
            if (chunk_size > 16) {
                input.seekg(static_cast<std::streamoff>(chunk_size - 16), std::ios::cur);
            }
        } else if (chunk_id == "data") {
            data_chunk.resize(chunk_size);
            input.read(reinterpret_cast<char*>(data_chunk.data()), static_cast<std::streamsize>(chunk_size));
        } else {
            input.seekg(static_cast<std::streamoff>(chunk_size), std::ios::cur);
        }
        if (chunk_size % 2 == 1) {
            input.seekg(1, std::ios::cur);
        }
    }

    if (audio_format != 1 || bits_per_sample != 16 || channels == 0 || sample_rate == 0 || data_chunk.empty()) {
        throw std::runtime_error("Only 16-bit PCM WAV reference audio is supported.");
    }

    WavData wav;
    wav.sample_rate = static_cast<int>(sample_rate);
    wav.channels = static_cast<int>(channels);
    const size_t sample_count = data_chunk.size() / sizeof(int16_t);
    wav.interleaved.resize(sample_count);
    for (size_t index = 0; index < sample_count; ++index) {
        const int16_t value = static_cast<int16_t>(
            static_cast<uint16_t>(data_chunk[index * 2]) | (static_cast<uint16_t>(data_chunk[index * 2 + 1]) << 8));
        wav.interleaved[index] = static_cast<float>(value) / 32768.0f;
    }
    return wav;
}

std::vector<float> linear_resample(const std::vector<float>& input, int src_rate, int dst_rate) {
    if (src_rate == dst_rate || input.empty()) {
        return input;
    }
    const double scale = static_cast<double>(dst_rate) / static_cast<double>(src_rate);
    const size_t output_size =
        static_cast<size_t>(std::max<int64_t>(1, static_cast<int64_t>(std::llround(input.size() * scale))));
    std::vector<float> output(output_size, 0.0f);
    for (size_t index = 0; index < output_size; ++index) {
        const double src_pos = static_cast<double>(index) / scale;
        const size_t left = static_cast<size_t>(std::floor(src_pos));
        const size_t right = std::min(left + 1, input.size() - 1);
        const double frac = src_pos - static_cast<double>(left);
        output[index] = static_cast<float>((1.0 - frac) * input[left] + frac * input[right]);
    }
    return output;
}

std::vector<float> wav_to_channel_major(const WavData& wav, int target_rate, int target_channels) {
    const size_t frames = wav.interleaved.size() / static_cast<size_t>(wav.channels);
    std::vector<std::vector<float>> per_channel(static_cast<size_t>(wav.channels), std::vector<float>(frames, 0.0f));
    for (size_t frame = 0; frame < frames; ++frame) {
        for (int channel = 0; channel < wav.channels; ++channel) {
            per_channel[static_cast<size_t>(channel)][frame] =
                wav.interleaved[frame * static_cast<size_t>(wav.channels) + static_cast<size_t>(channel)];
        }
    }

    if (wav.channels > 1 && target_channels == 1) {
        std::vector<float> mono(frames, 0.0f);
        for (size_t frame = 0; frame < frames; ++frame) {
            float sum = 0.0f;
            for (int channel = 0; channel < wav.channels; ++channel) {
                sum += per_channel[static_cast<size_t>(channel)][frame];
            }
            mono[frame] = sum / static_cast<float>(wav.channels);
        }
        per_channel = {mono};
    } else if (wav.channels == 1 && target_channels > 1) {
        per_channel.resize(static_cast<size_t>(target_channels), per_channel[0]);
    } else if (wav.channels != target_channels) {
        throw std::runtime_error("Unsupported reference-audio channel conversion.");
    }

    for (std::vector<float>& channel_samples : per_channel) {
        channel_samples = linear_resample(channel_samples, wav.sample_rate, target_rate);
    }

    const size_t samples_per_channel = per_channel.empty() ? 0 : per_channel[0].size();
    std::vector<float> output(static_cast<size_t>(target_channels) * samples_per_channel, 0.0f);
    for (int channel = 0; channel < target_channels; ++channel) {
        std::copy(per_channel[static_cast<size_t>(channel)].begin(),
                  per_channel[static_cast<size_t>(channel)].end(),
                  output.begin() + static_cast<std::ptrdiff_t>(static_cast<size_t>(channel) * samples_per_channel));
    }
    return output;
}

std::vector<float> extract_last_hidden(const Tensor& tensor) {
    if (tensor.dtype != DType::Float32) {
        throw std::runtime_error("global_hidden must be float32.");
    }
    if (tensor.shape.size() == 2) {
        return tensor.data_f32;
    }
    if (tensor.shape.size() != 3 || tensor.shape[0] != 1) {
        throw std::runtime_error("Unexpected global_hidden shape.");
    }
    const int64_t seq_len = tensor.shape[1];
    const int64_t hidden = tensor.shape[2];
    const size_t start = static_cast<size_t>((seq_len - 1) * hidden);
    return std::vector<float>(tensor.data_f32.begin() + static_cast<std::ptrdiff_t>(start),
                              tensor.data_f32.begin() + static_cast<std::ptrdiff_t>(start + hidden));
}

int tensor_scalar_int(const Tensor& tensor) {
    switch (tensor.dtype) {
        case DType::UInt8:
            return tensor.data_u8.empty() ? 0 : static_cast<int>(tensor.data_u8[0]);
        case DType::Int32:
            return tensor.data_i32.empty() ? 0 : tensor.data_i32[0];
        case DType::Int64:
            return tensor.data_i64.empty() ? 0 : static_cast<int>(tensor.data_i64[0]);
        case DType::Float32:
            return tensor.data_f32.empty() ? 0 : static_cast<int>(std::lround(tensor.data_f32[0]));
    }
    return 0;
}

std::vector<int32_t> tensor_to_i32(const Tensor& tensor) {
    switch (tensor.dtype) {
        case DType::Int32:
            return tensor.data_i32;
        case DType::Int64: {
            std::vector<int32_t> result;
            result.reserve(tensor.data_i64.size());
            for (int64_t value : tensor.data_i64) {
                result.push_back(static_cast<int32_t>(value));
            }
            return result;
        }
        case DType::UInt8: {
            std::vector<int32_t> result;
            result.reserve(tensor.data_u8.size());
            for (uint8_t value : tensor.data_u8) {
                result.push_back(static_cast<int32_t>(value));
            }
            return result;
        }
        case DType::Float32: {
            std::vector<int32_t> result;
            result.reserve(tensor.data_f32.size());
            for (float value : tensor.data_f32) {
                result.push_back(static_cast<int32_t>(std::lround(value)));
            }
            return result;
        }
    }
    return {};
}

struct ManifestConfig {
    fs::path manifest_path;
    fs::path manifest_dir;
    fs::path tts_meta_path;
    fs::path codec_meta_path;
    fs::path tokenizer_model_path;
    int n_vq = 16;
    int row_width = 17;
    int audio_pad_token_id = 1024;
    int audio_start_token_id = 6;
    int audio_end_token_id = 7;
    int audio_user_slot_token_id = 8;
    int audio_assistant_slot_token_id = 9;
    int audio_codebook_size = 1024;
    int default_max_new_frames = 375;
    std::vector<int32_t> user_prompt_prefix_token_ids;
    std::vector<int32_t> user_prompt_after_reference_token_ids;
    std::vector<int32_t> assistant_prompt_prefix_token_ids;
    std::unordered_map<std::string, std::vector<std::vector<int32_t>>> builtin_voice_prompt_codes;
    std::string default_voice;
};

struct TtsMetaConfig {
    fs::path tts_dir;
    fs::path prefill_path;
    fs::path decode_step_path;
    fs::path local_fixed_sampled_frame_path;
    std::vector<std::string> prefill_output_names;
    std::vector<std::string> decode_input_names;
    std::vector<std::string> decode_output_names;
};

struct CodecMetaConfig {
    fs::path codec_dir;
    fs::path encode_path;
    fs::path decode_full_path;
    int sample_rate = 48000;
    int channels = 2;
    int num_quantizers = 16;
};

fs::path resolve_manifest_path(const fs::path& model_dir) {
    const std::vector<fs::path> candidates = {
        model_dir / "browser_poc_manifest.json",
        model_dir / "MOSS-TTS-Nano-100M-ONNX" / "browser_poc_manifest.json",
        model_dir / "MOSS-TTS-Nano-ONNX-CPU" / "browser_poc_manifest.json",
    };
    for (const fs::path& candidate : candidates) {
        if (fs::exists(candidate)) {
            return fs::weakly_canonical(candidate);
        }
    }
    throw std::runtime_error("browser_poc_manifest.json not found under model directory.");
}

fs::path resolve_relative_path(const fs::path& base_dir, const std::string& relative) {
    return fs::weakly_canonical(base_dir / fs::path(relative));
}

ManifestConfig load_manifest_config(const fs::path& model_dir) {
    ManifestConfig cfg;
    cfg.manifest_path = resolve_manifest_path(model_dir);
    cfg.manifest_dir = cfg.manifest_path.parent_path();
    const JsonValue root_value = parse_json_file(cfg.manifest_path);
    const JsonObject& root = root_value.as_object();

    const JsonObject& model_files = object_field(root, "model_files").as_object();
    cfg.tts_meta_path = resolve_relative_path(cfg.manifest_dir, as_string(object_field(model_files, "tts_meta")));
    cfg.codec_meta_path = resolve_relative_path(cfg.manifest_dir, as_string(object_field(model_files, "codec_meta")));
    cfg.tokenizer_model_path = resolve_relative_path(cfg.manifest_dir, as_string(object_field(model_files, "tokenizer_model")));

    const JsonObject& tts_config = object_field(root, "tts_config").as_object();
    cfg.n_vq = as_int(object_field(tts_config, "n_vq"));
    cfg.row_width = cfg.n_vq + 1;
    cfg.audio_pad_token_id = as_int(object_field(tts_config, "audio_pad_token_id"));
    cfg.audio_start_token_id = as_int(object_field(tts_config, "audio_start_token_id"));
    cfg.audio_end_token_id = as_int(object_field(tts_config, "audio_end_token_id"));
    cfg.audio_user_slot_token_id = as_int(object_field(tts_config, "audio_user_slot_token_id"));
    cfg.audio_assistant_slot_token_id = as_int(object_field(tts_config, "audio_assistant_slot_token_id"));
    cfg.audio_codebook_size = as_int(object_field(tts_config, "audio_codebook_sizes").as_array().front());

    const JsonObject& generation_defaults = object_field(root, "generation_defaults").as_object();
    cfg.default_max_new_frames = as_int(object_field(generation_defaults, "max_new_frames"));

    const JsonObject& prompt_templates = object_field(root, "prompt_templates").as_object();
    cfg.user_prompt_prefix_token_ids = as_i32_array(object_field(prompt_templates, "user_prompt_prefix_token_ids"));
    cfg.user_prompt_after_reference_token_ids =
        as_i32_array(object_field(prompt_templates, "user_prompt_after_reference_token_ids"));
    cfg.assistant_prompt_prefix_token_ids = as_i32_array(object_field(prompt_templates, "assistant_prompt_prefix_token_ids"));

    const JsonArray& builtin_voices = object_field(root, "builtin_voices").as_array();
    for (const JsonValue& voice_value : builtin_voices) {
        const JsonObject& voice_object = voice_value.as_object();
        const std::string voice_name = as_string(object_field(voice_object, "voice"));
        if (cfg.default_voice.empty()) {
            cfg.default_voice = voice_name;
        }
        std::vector<std::vector<int32_t>> prompt_codes;
        for (const JsonValue& row_value : object_field(voice_object, "prompt_audio_codes").as_array()) {
            prompt_codes.push_back(as_i32_array(row_value));
        }
        cfg.builtin_voice_prompt_codes.emplace(voice_name, std::move(prompt_codes));
    }
    return cfg;
}

TtsMetaConfig load_tts_meta_config(const fs::path& path) {
    TtsMetaConfig cfg;
    cfg.tts_dir = path.parent_path();
    const JsonValue root_value = parse_json_file(path);
    const JsonObject& root = root_value.as_object();
    const JsonObject& files = object_field(root, "files").as_object();
    cfg.prefill_path = resolve_relative_path(cfg.tts_dir, as_string(object_field(files, "prefill")));
    cfg.decode_step_path = resolve_relative_path(cfg.tts_dir, as_string(object_field(files, "decode_step")));
    cfg.local_fixed_sampled_frame_path =
        resolve_relative_path(cfg.tts_dir, as_string(object_field(files, "local_fixed_sampled_frame")));

    const JsonObject& onnx = object_field(root, "onnx").as_object();
    cfg.prefill_output_names = as_string_array(object_field(onnx, "prefill_output_names"));
    cfg.decode_input_names = as_string_array(object_field(onnx, "decode_input_names"));
    cfg.decode_output_names = as_string_array(object_field(onnx, "decode_output_names"));
    return cfg;
}

CodecMetaConfig load_codec_meta_config(const fs::path& path) {
    CodecMetaConfig cfg;
    cfg.codec_dir = path.parent_path();
    const JsonValue root_value = parse_json_file(path);
    const JsonObject& root = root_value.as_object();
    const JsonObject& files = object_field(root, "files").as_object();
    cfg.encode_path = resolve_relative_path(cfg.codec_dir, as_string(object_field(files, "encode")));
    cfg.decode_full_path = resolve_relative_path(cfg.codec_dir, as_string(object_field(files, "decode_full")));

    const JsonObject& codec_config = object_field(root, "codec_config").as_object();
    cfg.sample_rate = as_int(object_field(codec_config, "sample_rate"));
    cfg.channels = as_int(object_field(codec_config, "channels"));
    cfg.num_quantizers = as_int(object_field(codec_config, "num_quantizers"));
    return cfg;
}

struct RequestRows {
    std::vector<std::vector<int32_t>> input_ids;
    std::vector<std::vector<int32_t>> attention_mask;
};

std::vector<std::vector<int32_t>> build_text_rows(const std::vector<int32_t>& token_ids, const ManifestConfig& manifest) {
    std::vector<std::vector<int32_t>> rows;
    rows.reserve(token_ids.size());
    for (int32_t token_id : token_ids) {
        std::vector<int32_t> row(static_cast<size_t>(manifest.row_width), manifest.audio_pad_token_id);
        row[0] = token_id;
        rows.push_back(std::move(row));
    }
    return rows;
}

std::vector<std::vector<int32_t>> build_audio_prefix_rows(
    const std::vector<std::vector<int32_t>>& prompt_audio_codes,
    const ManifestConfig& manifest) {
    std::vector<std::vector<int32_t>> rows;
    rows.reserve(prompt_audio_codes.size());
    for (const std::vector<int32_t>& code_row : prompt_audio_codes) {
        std::vector<int32_t> row(static_cast<size_t>(manifest.row_width), manifest.audio_pad_token_id);
        row[0] = manifest.audio_user_slot_token_id;
        for (int channel = 0; channel < std::min(manifest.n_vq, static_cast<int>(code_row.size())); ++channel) {
            row[static_cast<size_t>(channel + 1)] = code_row[static_cast<size_t>(channel)];
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

RequestRows build_voice_clone_request_rows(
    const ManifestConfig& manifest,
    const std::vector<std::vector<int32_t>>& prompt_audio_codes,
    const std::vector<int32_t>& text_token_ids) {
    std::vector<int32_t> prefix_text = manifest.user_prompt_prefix_token_ids;
    prefix_text.push_back(manifest.audio_start_token_id);

    std::vector<int32_t> suffix_text = {manifest.audio_end_token_id};
    suffix_text.insert(suffix_text.end(),
                       manifest.user_prompt_after_reference_token_ids.begin(),
                       manifest.user_prompt_after_reference_token_ids.end());
    suffix_text.insert(suffix_text.end(), text_token_ids.begin(), text_token_ids.end());
    suffix_text.insert(suffix_text.end(),
                       manifest.assistant_prompt_prefix_token_ids.begin(),
                       manifest.assistant_prompt_prefix_token_ids.end());
    suffix_text.push_back(manifest.audio_start_token_id);

    RequestRows request;
    auto prefix_rows = build_text_rows(prefix_text, manifest);
    auto audio_rows = build_audio_prefix_rows(prompt_audio_codes, manifest);
    auto suffix_rows = build_text_rows(suffix_text, manifest);
    request.input_ids.reserve(prefix_rows.size() + audio_rows.size() + suffix_rows.size());
    request.input_ids.insert(request.input_ids.end(), prefix_rows.begin(), prefix_rows.end());
    request.input_ids.insert(request.input_ids.end(), audio_rows.begin(), audio_rows.end());
    request.input_ids.insert(request.input_ids.end(), suffix_rows.begin(), suffix_rows.end());
    request.attention_mask = {std::vector<int32_t>(request.input_ids.size(), 1)};
    return request;
}

Tensor make_i32_tensor_3d(const std::vector<std::vector<int32_t>>& rows, int row_width) {
    const size_t seq_len = rows.size();
    std::vector<int32_t> flat(seq_len * static_cast<size_t>(row_width), 0);
    size_t offset = 0;
    for (const std::vector<int32_t>& row : rows) {
        for (int column = 0; column < row_width; ++column) {
            flat[offset++] = row[static_cast<size_t>(column)];
        }
    }
    return Tensor(std::move(flat), {1, static_cast<int64_t>(seq_len), static_cast<int64_t>(row_width)});
}

Tensor make_i32_tensor_2d(const std::vector<std::vector<int32_t>>& rows) {
    const size_t dim0 = rows.size();
    const size_t dim1 = rows.empty() ? 0 : rows.front().size();
    std::vector<int32_t> flat(dim0 * dim1, 0);
    size_t offset = 0;
    for (const std::vector<int32_t>& row : rows) {
        for (int32_t value : row) {
            flat[offset++] = value;
        }
    }
    return Tensor(std::move(flat), {static_cast<int64_t>(dim0), static_cast<int64_t>(dim1)});
}

std::vector<float> clamp_randoms(std::mt19937& rng, int count) {
    std::uniform_real_distribution<float> dist(0.0f, 0.99999994f);
    std::vector<float> values(static_cast<size_t>(count), 0.0f);
    for (float& value : values) {
        value = dist(rng);
    }
    return values;
}

std::vector<float> channel_major_to_interleaved(const Tensor& tensor, int audio_length) {
    if (tensor.dtype != DType::Float32 || tensor.shape.size() != 3 || tensor.shape[0] != 1) {
        throw std::runtime_error("Unexpected decoded audio tensor shape.");
    }
    const int channels = static_cast<int>(tensor.shape[1]);
    const int samples = std::min(audio_length, static_cast<int>(tensor.shape[2]));
    std::vector<float> output(static_cast<size_t>(channels) * static_cast<size_t>(samples), 0.0f);
    for (int sample = 0; sample < samples; ++sample) {
        for (int channel = 0; channel < channels; ++channel) {
            const size_t source_index =
                static_cast<size_t>(channel) * static_cast<size_t>(tensor.shape[2]) + static_cast<size_t>(sample);
            output[static_cast<size_t>(sample) * static_cast<size_t>(channels) + static_cast<size_t>(channel)] =
                tensor.data_f32[source_index];
        }
    }
    return output;
}

}  // namespace

struct MossTtsPipeline::Impl {
    ManifestConfig manifest;
    TtsMetaConfig tts_meta;
    CodecMetaConfig codec_meta;
    Tokenizer tokenizer;
    std::unique_ptr<InferBackend> prefill;
    std::unique_ptr<InferBackend> decode_step;
    std::unique_ptr<InferBackend> local_fixed_sampled_frame;
    std::unique_ptr<InferBackend> codec_encode;
    std::unique_ptr<InferBackend> codec_decode_full;
    double last_inference_seconds = 0.0;

    bool ready() const {
        return tokenizer.is_loaded() && prefill && decode_step && local_fixed_sampled_frame && codec_encode &&
               codec_decode_full && prefill->is_loaded() && decode_step->is_loaded() &&
               local_fixed_sampled_frame->is_loaded() && codec_encode->is_loaded() && codec_decode_full->is_loaded();
    }

    std::vector<std::vector<int32_t>> encode_reference_audio(const fs::path& wav_path) const {
        const WavData wav = load_wav(wav_path);
        const std::vector<float> waveform = wav_to_channel_major(wav, codec_meta.sample_rate, codec_meta.channels);
        const int64_t samples_per_channel =
            static_cast<int64_t>(waveform.size() / static_cast<size_t>(codec_meta.channels));
        auto outputs = codec_encode->run(
            {
                {"waveform", Tensor(waveform, {1, codec_meta.channels, samples_per_channel})},
                {"input_lengths", Tensor(std::vector<int32_t>{static_cast<int32_t>(samples_per_channel)}, {1})},
            },
            {"audio_codes", "audio_code_lengths"});
        const int code_length = tensor_scalar_int(outputs.at("audio_code_lengths"));
        const std::vector<int32_t> flat_codes = tensor_to_i32(outputs.at("audio_codes"));
        std::vector<std::vector<int32_t>> prompt_codes;
        prompt_codes.reserve(static_cast<size_t>(code_length));
        for (int frame = 0; frame < code_length; ++frame) {
            std::vector<int32_t> row(static_cast<size_t>(codec_meta.num_quantizers), 0);
            for (int quantizer = 0; quantizer < codec_meta.num_quantizers; ++quantizer) {
                row[static_cast<size_t>(quantizer)] =
                    flat_codes[static_cast<size_t>(frame * codec_meta.num_quantizers + quantizer)];
            }
            prompt_codes.push_back(std::move(row));
        }
        return prompt_codes;
    }

    std::vector<std::vector<int32_t>> resolve_prompt_audio_codes(const GenerateParams& params) const {
        if (!params.prompt_wav_path.empty()) {
            return encode_reference_audio(fs::path(params.prompt_wav_path));
        }
        const std::string voice = params.voice.empty() ? manifest.default_voice : params.voice;
        const auto found = manifest.builtin_voice_prompt_codes.find(voice);
        if (found == manifest.builtin_voice_prompt_codes.end()) {
            throw std::runtime_error("Built-in voice not found: " + voice);
        }
        return found->second;
    }

    std::vector<float> run_generation(const GenerateParams& params) {
        const auto start_time = std::chrono::high_resolution_clock::now();
        const std::vector<int32_t> text_token_ids = tokenizer.encode(params.text);
        if (text_token_ids.empty()) {
            throw std::runtime_error("Tokenizer produced no tokens for input text.");
        }

        const std::vector<std::vector<int32_t>> prompt_audio_codes = resolve_prompt_audio_codes(params);
        const RequestRows request_rows = build_voice_clone_request_rows(manifest, prompt_audio_codes, text_token_ids);
        auto prefill_outputs = prefill->run(
            {
                {"input_ids", make_i32_tensor_3d(request_rows.input_ids, manifest.row_width)},
                {"attention_mask", make_i32_tensor_2d(request_rows.attention_mask)},
            },
            tts_meta.prefill_output_names);

        std::vector<float> global_hidden = extract_last_hidden(prefill_outputs.at("global_hidden"));
        std::unordered_map<std::string, Tensor> past_by_name;
        for (size_t index = 1; index < tts_meta.prefill_output_names.size(); ++index) {
            const std::string present_name = tts_meta.prefill_output_names[index];
            std::string past_name = present_name;
            const size_t pos = past_name.find("present_");
            if (pos != std::string::npos) {
                past_name.replace(pos, 8, "past_");
            }
            past_by_name.emplace(std::move(past_name), prefill_outputs.at(present_name));
        }
        int32_t past_valid_length = static_cast<int32_t>(request_rows.attention_mask.front().size());

        const int max_new_frames = params.max_new_frames > 0 ? params.max_new_frames : manifest.default_max_new_frames;
        std::mt19937 rng(static_cast<uint32_t>(params.seed));
        std::vector<std::set<int32_t>> previous_token_sets(static_cast<size_t>(manifest.n_vq));
        std::vector<std::vector<int32_t>> generated_frames;
        generated_frames.reserve(static_cast<size_t>(max_new_frames));

        for (int step = 0; step < max_new_frames; ++step) {
            std::vector<int32_t> repetition_mask(
                static_cast<size_t>(manifest.n_vq) * static_cast<size_t>(manifest.audio_codebook_size), 0);
            for (int channel = 0; channel < manifest.n_vq; ++channel) {
                for (int32_t token_id : previous_token_sets[static_cast<size_t>(channel)]) {
                    if (token_id >= 0 && token_id < manifest.audio_codebook_size) {
                        repetition_mask[static_cast<size_t>(channel) * static_cast<size_t>(manifest.audio_codebook_size) +
                                        static_cast<size_t>(token_id)] = 1;
                    }
                }
            }

            auto local_outputs = local_fixed_sampled_frame->run(
                {
                    {"global_hidden", Tensor(global_hidden, {1, static_cast<int64_t>(global_hidden.size())})},
                    {"repetition_seen_mask",
                     Tensor(std::move(repetition_mask),
                            {1, static_cast<int64_t>(manifest.n_vq), static_cast<int64_t>(manifest.audio_codebook_size)})},
                    {"assistant_random_u", Tensor(clamp_randoms(rng, 1), {1})},
                    {"audio_random_u", Tensor(clamp_randoms(rng, manifest.n_vq), {1, manifest.n_vq})},
                },
                {"should_continue", "frame_token_ids"});

            if (tensor_scalar_int(local_outputs.at("should_continue")) == 0) {
                break;
            }

            std::vector<int32_t> frame_token_ids = tensor_to_i32(local_outputs.at("frame_token_ids"));
            if (frame_token_ids.size() < static_cast<size_t>(manifest.n_vq)) {
                throw std::runtime_error("local_fixed_sampled_frame returned an incomplete frame.");
            }
            frame_token_ids.resize(static_cast<size_t>(manifest.n_vq));
            generated_frames.push_back(frame_token_ids);
            for (int channel = 0; channel < manifest.n_vq; ++channel) {
                previous_token_sets[static_cast<size_t>(channel)].insert(frame_token_ids[static_cast<size_t>(channel)]);
            }

            std::vector<std::vector<int32_t>> next_rows = {
                std::vector<int32_t>(static_cast<size_t>(manifest.row_width), manifest.audio_pad_token_id)};
            next_rows[0][0] = manifest.audio_assistant_slot_token_id;
            for (int channel = 0; channel < manifest.n_vq; ++channel) {
                next_rows[0][static_cast<size_t>(channel + 1)] = frame_token_ids[static_cast<size_t>(channel)];
            }

            std::unordered_map<std::string, Tensor> decode_inputs;
            decode_inputs.emplace("input_ids", make_i32_tensor_3d(next_rows, manifest.row_width));
            decode_inputs.emplace("past_valid_lengths", Tensor(std::vector<int32_t>{past_valid_length}, {1}));
            for (size_t index = 2; index < tts_meta.decode_input_names.size(); ++index) {
                const std::string& input_name = tts_meta.decode_input_names[index];
                decode_inputs.emplace(input_name, past_by_name.at(input_name));
            }

            auto decode_outputs = decode_step->run(decode_inputs, tts_meta.decode_output_names);
            global_hidden = extract_last_hidden(decode_outputs.at("global_hidden"));
            ++past_valid_length;
            past_by_name.clear();
            for (size_t index = 1; index < tts_meta.decode_output_names.size(); ++index) {
                const std::string present_name = tts_meta.decode_output_names[index];
                std::string past_name = present_name;
                const size_t pos = past_name.find("present_");
                if (pos != std::string::npos) {
                    past_name.replace(pos, 8, "past_");
                }
                past_by_name.emplace(std::move(past_name), decode_outputs.at(present_name));
            }
        }

        if (generated_frames.empty()) {
            last_inference_seconds =
                std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - start_time).count();
            return {};
        }

        std::vector<int32_t> flat_audio_codes;
        flat_audio_codes.reserve(generated_frames.size() * static_cast<size_t>(codec_meta.num_quantizers));
        for (const std::vector<int32_t>& frame : generated_frames) {
            flat_audio_codes.insert(flat_audio_codes.end(), frame.begin(), frame.end());
        }

        auto codec_outputs = codec_decode_full->run(
            {
                {"audio_codes",
                 Tensor(std::move(flat_audio_codes),
                        {1, static_cast<int64_t>(generated_frames.size()), static_cast<int64_t>(codec_meta.num_quantizers)})},
                {"audio_code_lengths", Tensor(std::vector<int32_t>{static_cast<int32_t>(generated_frames.size())}, {1})},
            },
            {"audio", "audio_lengths"});

        const int audio_length = tensor_scalar_int(codec_outputs.at("audio_lengths"));
        last_inference_seconds =
            std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - start_time).count();
        return channel_major_to_interleaved(codec_outputs.at("audio"), audio_length);
    }
};

MossTtsPipeline::MossTtsPipeline() : impl_(std::make_unique<Impl>()) {}
MossTtsPipeline::~MossTtsPipeline() = default;

bool MossTtsPipeline::init(const std::string& model_dir) {
    try {
        impl_->manifest = load_manifest_config(fs::path(model_dir));
        impl_->tts_meta = load_tts_meta_config(impl_->manifest.tts_meta_path);
        impl_->codec_meta = load_codec_meta_config(impl_->manifest.codec_meta_path);

        if (!impl_->tokenizer.load(impl_->manifest.tokenizer_model_path.string())) {
            return false;
        }

        impl_->prefill = create_backend();
        impl_->prefill->load_model(impl_->tts_meta.prefill_path.string());
        impl_->decode_step = create_backend();
        impl_->decode_step->load_model(impl_->tts_meta.decode_step_path.string());
        impl_->local_fixed_sampled_frame = create_backend();
        impl_->local_fixed_sampled_frame->load_model(impl_->tts_meta.local_fixed_sampled_frame_path.string());
        impl_->codec_encode = create_backend();
        impl_->codec_encode->load_model(impl_->codec_meta.encode_path.string());
        impl_->codec_decode_full = create_backend();
        impl_->codec_decode_full->load_model(impl_->codec_meta.decode_full_path.string());
        return impl_->ready();
    } catch (const std::exception& exc) {
        std::cerr << "[MOSS-TTS] init failed: " << exc.what() << std::endl;
        return false;
    }
}

std::vector<float> MossTtsPipeline::generate(const GenerateParams& params) {
    if (!impl_->ready()) {
        throw std::runtime_error("MOSS-TTS pipeline is not initialized.");
    }
    return impl_->run_generation(params);
}

bool MossTtsPipeline::is_ready() const {
    return impl_->ready();
}

int MossTtsPipeline::sample_rate() const {
    return impl_->codec_meta.sample_rate;
}

int MossTtsPipeline::channels() const {
    return impl_->codec_meta.channels;
}

double MossTtsPipeline::last_inference_seconds() const {
    return impl_->last_inference_seconds;
}

}  // namespace moss_tts
