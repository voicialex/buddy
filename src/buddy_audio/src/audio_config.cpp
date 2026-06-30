#include "buddy_audio/audio_config.hpp"

#include <algorithm>
#include <filesystem>
#include <sstream>
#include <string_view>

namespace {

std::string resolve_path(const std::string& models_dir, const std::string& model_dir, const std::string& filename) {
    if (filename.empty()) {
        return "";
    }
    if (!filename.empty() && filename[0] == '/') {
        return filename;
    }
    return (std::filesystem::path(models_dir) / model_dir / filename).string();
}

std::string resolve_path_list(const std::string& models_dir, const std::string& model_dir, const std::string& csv) {
    if (csv.empty()) {
        return "";
    }
    std::string result;
    std::istringstream iss(csv);
    std::string part;
    while (std::getline(iss, part, ',')) {
        if (!result.empty()) {
            result += ',';
        }
        result += resolve_path(models_dir, model_dir, part);
    }
    return result;
}

std::string choose_nonempty(const std::string& preferred, const std::string& fallback) {
    return preferred.empty() ? fallback : preferred;
}

bool has_override(
    const std::map<std::string, rclcpp::ParameterValue>& overrides,
    std::string_view key) {
    return overrides.find(std::string(key)) != overrides.end();
}

std::string choose_string_param(
    rclcpp_lifecycle::LifecycleNode& node,
    const std::map<std::string, rclcpp::ParameterValue>& overrides,
    std::string_view preferred_key,
    std::string_view fallback_key) {
    const auto preferred = node.get_parameter(std::string(preferred_key)).as_string();
    const auto fallback = node.get_parameter(std::string(fallback_key)).as_string();
    if (has_override(overrides, preferred_key)) {
        return preferred;
    }
    if (has_override(overrides, fallback_key)) {
        return fallback;
    }
    return choose_nonempty(preferred, fallback);
}

int choose_int_param(
    rclcpp_lifecycle::LifecycleNode& node,
    const std::map<std::string, rclcpp::ParameterValue>& overrides,
    std::string_view preferred_key,
    std::string_view fallback_key) {
    const int preferred = static_cast<int>(node.get_parameter(std::string(preferred_key)).as_int());
    const int fallback = static_cast<int>(node.get_parameter(std::string(fallback_key)).as_int());
    if (has_override(overrides, preferred_key)) {
        return preferred;
    }
    if (has_override(overrides, fallback_key)) {
        return fallback;
    }
    return preferred;
}

float choose_float_param(
    rclcpp_lifecycle::LifecycleNode& node,
    const std::map<std::string, rclcpp::ParameterValue>& overrides,
    std::string_view preferred_key,
    std::string_view fallback_key) {
    const float preferred = static_cast<float>(node.get_parameter(std::string(preferred_key)).as_double());
    const float fallback = static_cast<float>(node.get_parameter(std::string(fallback_key)).as_double());
    if (has_override(overrides, preferred_key)) {
        return preferred;
    }
    if (has_override(overrides, fallback_key)) {
        return fallback;
    }
    return preferred;
}

}  // namespace

void declare_audio_parameters(rclcpp_lifecycle::LifecycleNode& node) {
    node.declare_parameter("models_dir", "");
    node.declare_parameter("mic_device", "default");
    node.declare_parameter("speaker_device", "default");
    node.declare_parameter("sample_rate", 16000);
    node.declare_parameter("preprocess.webrtc.enable", true);
    node.declare_parameter("preprocess.webrtc.vad.enable", true);
    node.declare_parameter("preprocess.webrtc.vad.gate.enable", true);
    node.declare_parameter("preprocess.webrtc.vad.gate.with_kws", false);
    node.declare_parameter("preprocess.webrtc.vad.likelihood", 2);
    node.declare_parameter("preprocess.webrtc.vad.frame_ms", 10);
    node.declare_parameter("preprocess.webrtc.vad.min_voice_frames", 2);
    node.declare_parameter("preprocess.webrtc.vad.hangover_frames", 18);
    node.declare_parameter("preprocess.webrtc.ns.enable", true);
    node.declare_parameter("preprocess.webrtc.ns.level", 2);
    node.declare_parameter("preprocess.webrtc.agc.enable", true);
    node.declare_parameter("preprocess.webrtc.agc.mode", 2);
    node.declare_parameter("preprocess.webrtc.agc.target_level_dbfs", 6);
    node.declare_parameter("preprocess.webrtc.agc.compression_gain_db", 6);
    node.declare_parameter("preprocess.webrtc.agc.limiter_enable", true);
    node.declare_parameter("preprocess.webrtc.agc.analog_level_min", 0);
    node.declare_parameter("preprocess.webrtc.agc.analog_level_max", 255);
    node.declare_parameter("preprocess.webrtc.agc.stream_analog_level", 127);
    node.declare_parameter("preprocess.webrtc.aec.enable", true);
    node.declare_parameter("preprocess.webrtc.aec.suppression_level", 1);
    node.declare_parameter("preprocess.webrtc.aec.stream_delay_ms", 80);
    node.declare_parameter("kws.enable", false);
    node.declare_parameter("kws.model_dir", "");
    node.declare_parameter("kws.model.encoder", "");
    node.declare_parameter("kws.model.decoder", "");
    node.declare_parameter("kws.model.joiner", "");
    node.declare_parameter("kws.model.tokens", "");
    node.declare_parameter("kws.keywords_file", "");
    node.declare_parameter("kws.threshold", 0.08f);
    node.declare_parameter("kws.score", 2.2f);
    node.declare_parameter("asr.mode", "local");
    node.declare_parameter("asr.engine", "sherpa-onnx");
    node.declare_parameter("asr.runtime", "onnxruntime");
    node.declare_parameter("asr.local.sherpa.model_dir", "");
    node.declare_parameter("asr.local.sherpa.model.encoder", "");
    node.declare_parameter("asr.local.sherpa.model.decoder", "");
    node.declare_parameter("asr.local.sherpa.model.joiner", "");
    node.declare_parameter("asr.local.sherpa.model.tokens", "");
    node.declare_parameter("asr.local.sherpa.decoding_method", "greedy_search");
    node.declare_parameter("asr.local.sherpa_onnx.model_dir", "");
    node.declare_parameter("asr.local.sherpa_onnx.model.encoder", "");
    node.declare_parameter("asr.local.sherpa_onnx.model.decoder", "");
    node.declare_parameter("asr.local.sherpa_onnx.model.joiner", "");
    node.declare_parameter("asr.local.sherpa_onnx.model.tokens", "");
    node.declare_parameter("asr.local.sherpa_onnx.decoding_method", "greedy_search");
    node.declare_parameter("asr.local.native.model_dir", "");
    node.declare_parameter("asr.local.native.model.encoder", "");
    node.declare_parameter("asr.local.native.model.decoder", "");
    node.declare_parameter("asr.local.native.model.joiner", "");
    node.declare_parameter("asr.local.native.model.tokens", "");
    node.declare_parameter("asr.local.zipformer_rknn.model_dir", "");
    node.declare_parameter("asr.local.zipformer_rknn.model.encoder", "");
    node.declare_parameter("asr.local.zipformer_rknn.model.decoder", "");
    node.declare_parameter("asr.local.zipformer_rknn.model.joiner", "");
    node.declare_parameter("asr.local.zipformer_rknn.model.tokens", "");
    node.declare_parameter("asr.server.funasr.url", "ws://127.0.0.1:10095");
    node.declare_parameter("asr.server.funasr.mode", "online");
    node.declare_parameter("tts.mode", "local");
    node.declare_parameter("tts.engine", "sherpa-onnx");
    node.declare_parameter("tts.runtime", "onnxruntime");
    node.declare_parameter("tts.server.chattts.url", "http://127.0.0.1:9880/tts");
    node.declare_parameter("tts.local.sherpa.model_type", "melo");
    node.declare_parameter("tts.local.sherpa.model_dir", "");
    node.declare_parameter("tts.local.sherpa.model", "");
    node.declare_parameter("tts.local.sherpa.tokens", "");
    node.declare_parameter("tts.local.sherpa.voices", "");
    node.declare_parameter("tts.local.sherpa.lexicon", "");
    node.declare_parameter("tts.local.sherpa.lang", "");
    node.declare_parameter("tts.local.sherpa.dict_dir", "");
    node.declare_parameter("tts.local.sherpa.data_dir", "");
    node.declare_parameter("tts.local.sherpa.rule_fsts", "");
    node.declare_parameter("tts.local.sherpa.sid", 0);
    node.declare_parameter("tts.local.sherpa.speed", 1.0);
    node.declare_parameter("tts.local.sherpa_onnx.model_type", "kokoro");
    node.declare_parameter("tts.local.sherpa_onnx.model_dir", "");
    node.declare_parameter("tts.local.sherpa_onnx.model", "");
    node.declare_parameter("tts.local.sherpa_onnx.tokens", "");
    node.declare_parameter("tts.local.sherpa_onnx.voices", "");
    node.declare_parameter("tts.local.sherpa_onnx.lexicon", "");
    node.declare_parameter("tts.local.sherpa_onnx.lang", "");
    node.declare_parameter("tts.local.sherpa_onnx.dict_dir", "");
    node.declare_parameter("tts.local.sherpa_onnx.data_dir", "");
    node.declare_parameter("tts.local.sherpa_onnx.rule_fsts", "");
    node.declare_parameter("tts.local.sherpa_onnx.sid", 0);
    node.declare_parameter("tts.local.sherpa_onnx.speed", 1.0);
    node.declare_parameter("tts.local.moss.model_dir", "");
    node.declare_parameter("tts.local.moss.voice", "");
    node.declare_parameter("tts.local.moss.max_new_frames", 375);
    node.declare_parameter("tts.local.moss.seed", 1234);
    node.declare_parameter("tts.local.moss_onnx.model_dir", "");
    node.declare_parameter("tts.local.moss_onnx.voice", "");
    node.declare_parameter("tts.local.moss_onnx.max_new_frames", 375);
    node.declare_parameter("tts.local.moss_onnx.seed", 1234);
    node.declare_parameter("tts.local.melo.model_dir", "");
    node.declare_parameter("tts.local.melo.speaker", "");
    node.declare_parameter("tts.local.melo.rank", "rank3");
    node.declare_parameter("tts.local.melo.bert_mode", "rknn");
    node.declare_parameter("tts.local.melo.speed", 1.0);
    node.declare_parameter("tts.local.melo.sdp_ratio", 0.0);
    node.declare_parameter("tts.local.melo.noise_scale", 0.6);
    node.declare_parameter("tts.local.melo.noise_scale_w", 0.8);
    node.declare_parameter("tts.local.melo_rknn.model_dir", "");
    node.declare_parameter("tts.local.melo_rknn.speaker", "");
    node.declare_parameter("tts.local.melo_rknn.rank", "rank3");
    node.declare_parameter("tts.local.melo_rknn.bert_mode", "rknn");
    node.declare_parameter("tts.local.melo_rknn.speed", 1.0);
    node.declare_parameter("tts.local.melo_rknn.sdp_ratio", 0.0);
    node.declare_parameter("tts.local.melo_rknn.noise_scale", 0.6);
    node.declare_parameter("tts.local.melo_rknn.noise_scale_w", 0.8);
    node.declare_parameter("asr_cooldown_ms", 300);
    node.declare_parameter("asr_wake_guard_ms", 700);
}

AudioConfig load_audio_config(rclcpp_lifecycle::LifecycleNode& node) {
    const auto& overrides = node.get_node_parameters_interface()->get_parameter_overrides();

    AudioConfig cfg;
    cfg.models_dir = node.get_parameter("models_dir").as_string();
    cfg.mic_device = node.get_parameter("mic_device").as_string();
    cfg.speaker_device = node.get_parameter("speaker_device").as_string();
    cfg.sample_rate = node.get_parameter("sample_rate").as_int();
    cfg.preprocess.enable = node.get_parameter("preprocess.webrtc.enable").as_bool();
    cfg.preprocess.vad_enable = node.get_parameter("preprocess.webrtc.vad.enable").as_bool();
    cfg.preprocess.vad_gate_enable = node.get_parameter("preprocess.webrtc.vad.gate.enable").as_bool();
    cfg.preprocess.vad_gate_with_kws = node.get_parameter("preprocess.webrtc.vad.gate.with_kws").as_bool();
    cfg.preprocess.vad_likelihood = static_cast<int>(node.get_parameter("preprocess.webrtc.vad.likelihood").as_int());
    cfg.preprocess.vad_frame_ms = static_cast<int>(node.get_parameter("preprocess.webrtc.vad.frame_ms").as_int());
    cfg.preprocess.vad_min_voice_frames =
        static_cast<int>(node.get_parameter("preprocess.webrtc.vad.min_voice_frames").as_int());
    cfg.preprocess.vad_hangover_frames =
        static_cast<int>(node.get_parameter("preprocess.webrtc.vad.hangover_frames").as_int());
    cfg.preprocess.ns_enable = node.get_parameter("preprocess.webrtc.ns.enable").as_bool();
    cfg.preprocess.ns_level = static_cast<int>(node.get_parameter("preprocess.webrtc.ns.level").as_int());
    cfg.preprocess.agc_enable = node.get_parameter("preprocess.webrtc.agc.enable").as_bool();
    cfg.preprocess.agc_mode = static_cast<int>(node.get_parameter("preprocess.webrtc.agc.mode").as_int());
    cfg.preprocess.agc_target_level_dbfs =
        static_cast<int>(node.get_parameter("preprocess.webrtc.agc.target_level_dbfs").as_int());
    cfg.preprocess.agc_compression_gain_db =
        static_cast<int>(node.get_parameter("preprocess.webrtc.agc.compression_gain_db").as_int());
    cfg.preprocess.agc_limiter_enable = node.get_parameter("preprocess.webrtc.agc.limiter_enable").as_bool();
    cfg.preprocess.agc_analog_level_min =
        static_cast<int>(node.get_parameter("preprocess.webrtc.agc.analog_level_min").as_int());
    cfg.preprocess.agc_analog_level_max =
        static_cast<int>(node.get_parameter("preprocess.webrtc.agc.analog_level_max").as_int());
    cfg.preprocess.agc_stream_analog_level =
        static_cast<int>(node.get_parameter("preprocess.webrtc.agc.stream_analog_level").as_int());
    cfg.preprocess.aec_enable = node.get_parameter("preprocess.webrtc.aec.enable").as_bool();
    cfg.preprocess.aec_suppression_level =
        static_cast<int>(node.get_parameter("preprocess.webrtc.aec.suppression_level").as_int());
    cfg.preprocess.aec_stream_delay_ms =
        static_cast<int>(node.get_parameter("preprocess.webrtc.aec.stream_delay_ms").as_int());
    cfg.kws_enabled = node.get_parameter("kws.enable").as_bool();
    const int cooldown_ms = static_cast<int>(node.get_parameter("asr_cooldown_ms").as_int());
    const int wake_guard_ms = static_cast<int>(node.get_parameter("asr_wake_guard_ms").as_int());
    cfg.asr_cooldown_chunks = std::max(1, cooldown_ms / 100);
    cfg.asr_wake_guard_chunks = std::max(0, wake_guard_ms / 100);

    const auto kws_model_dir = node.get_parameter("kws.model_dir").as_string();
    cfg.kws.encoder = resolve_path(cfg.models_dir, kws_model_dir, node.get_parameter("kws.model.encoder").as_string());
    cfg.kws.decoder = resolve_path(cfg.models_dir, kws_model_dir, node.get_parameter("kws.model.decoder").as_string());
    cfg.kws.joiner = resolve_path(cfg.models_dir, kws_model_dir, node.get_parameter("kws.model.joiner").as_string());
    cfg.kws.tokens = resolve_path(cfg.models_dir, kws_model_dir, node.get_parameter("kws.model.tokens").as_string());
    cfg.kws.keywords_file = resolve_path(cfg.models_dir, kws_model_dir, node.get_parameter("kws.keywords_file").as_string());
    cfg.kws.threshold = static_cast<float>(node.get_parameter("kws.threshold").as_double());
    cfg.kws.score = static_cast<float>(node.get_parameter("kws.score").as_double());

    cfg.asr_mode = node.get_parameter("asr.mode").as_string();
    cfg.asr_engine = node.get_parameter("asr.engine").as_string();
    cfg.asr_runtime = node.get_parameter("asr.runtime").as_string();
    cfg.asr_local_native.models_dir = cfg.models_dir;
    cfg.asr_local_native.sample_rate = cfg.sample_rate;
    cfg.asr_local_native.model_dir = choose_string_param(
        node, overrides,
        "asr.local.zipformer_rknn.model_dir",
        "asr.local.native.model_dir");
    cfg.asr_local_native.encoder = choose_string_param(
        node, overrides,
        "asr.local.zipformer_rknn.model.encoder",
        "asr.local.native.model.encoder");
    cfg.asr_local_native.decoder = choose_string_param(
        node, overrides,
        "asr.local.zipformer_rknn.model.decoder",
        "asr.local.native.model.decoder");
    cfg.asr_local_native.joiner = choose_string_param(
        node, overrides,
        "asr.local.zipformer_rknn.model.joiner",
        "asr.local.native.model.joiner");
    cfg.asr_local_native.tokens = choose_string_param(
        node, overrides,
        "asr.local.zipformer_rknn.model.tokens",
        "asr.local.native.model.tokens");

    cfg.asr_local_sherpa.models_dir = cfg.models_dir;
    cfg.asr_local_sherpa.sample_rate = cfg.sample_rate;
    cfg.asr_local_sherpa.model_dir = choose_string_param(
        node, overrides,
        "asr.local.sherpa_onnx.model_dir",
        "asr.local.sherpa.model_dir");
    cfg.asr_local_sherpa.encoder = choose_string_param(
        node, overrides,
        "asr.local.sherpa_onnx.model.encoder",
        "asr.local.sherpa.model.encoder");
    cfg.asr_local_sherpa.decoder = choose_string_param(
        node, overrides,
        "asr.local.sherpa_onnx.model.decoder",
        "asr.local.sherpa.model.decoder");
    cfg.asr_local_sherpa.joiner = choose_string_param(
        node, overrides,
        "asr.local.sherpa_onnx.model.joiner",
        "asr.local.sherpa.model.joiner");
    cfg.asr_local_sherpa.tokens = choose_string_param(
        node, overrides,
        "asr.local.sherpa_onnx.model.tokens",
        "asr.local.sherpa.model.tokens");
    cfg.asr_local_sherpa.decoding_method = choose_string_param(
        node, overrides,
        "asr.local.sherpa_onnx.decoding_method",
        "asr.local.sherpa.decoding_method");

    cfg.asr_server.models_dir = cfg.models_dir;
    cfg.asr_server.sample_rate = cfg.sample_rate;
    cfg.asr_server.server_url = node.get_parameter("asr.server.funasr.url").as_string();
    cfg.asr_server.server_mode = node.get_parameter("asr.server.funasr.mode").as_string();

    cfg.tts_mode = node.get_parameter("tts.mode").as_string();
    cfg.tts_engine = node.get_parameter("tts.engine").as_string();
    cfg.tts_runtime = node.get_parameter("tts.runtime").as_string();

    cfg.tts_server.models_dir = cfg.models_dir;
    cfg.tts_server.sample_rate = cfg.sample_rate;
    cfg.tts_server.server_url = node.get_parameter("tts.server.chattts.url").as_string();

    const auto tts_sherpa_dir = choose_string_param(
        node, overrides,
        "tts.local.sherpa_onnx.model_dir",
        "tts.local.sherpa.model_dir");
    cfg.tts_local_sherpa.models_dir = cfg.models_dir;
    cfg.tts_local_sherpa.sample_rate = cfg.sample_rate;
    cfg.tts_local_sherpa.model_dir = tts_sherpa_dir;
    cfg.tts_local_sherpa.model_type = choose_string_param(
        node, overrides,
        "tts.local.sherpa_onnx.model_type",
        "tts.local.sherpa.model_type");
    cfg.tts_local_sherpa.model = resolve_path(
        cfg.models_dir, tts_sherpa_dir,
        choose_string_param(
            node, overrides,
            "tts.local.sherpa_onnx.model",
            "tts.local.sherpa.model"));
    cfg.tts_local_sherpa.tokens = resolve_path(
        cfg.models_dir, tts_sherpa_dir,
        choose_string_param(
            node, overrides,
            "tts.local.sherpa_onnx.tokens",
            "tts.local.sherpa.tokens"));
    cfg.tts_local_sherpa.voices = resolve_path(
        cfg.models_dir, tts_sherpa_dir,
        choose_string_param(
            node, overrides,
            "tts.local.sherpa_onnx.voices",
            "tts.local.sherpa.voices"));
    cfg.tts_local_sherpa.lexicon = resolve_path(
        cfg.models_dir, tts_sherpa_dir,
        choose_string_param(
            node, overrides,
            "tts.local.sherpa_onnx.lexicon",
            "tts.local.sherpa.lexicon"));
    cfg.tts_local_sherpa.lang = choose_string_param(
        node, overrides,
        "tts.local.sherpa_onnx.lang",
        "tts.local.sherpa.lang");
    cfg.tts_local_sherpa.dict_dir = resolve_path(
        cfg.models_dir, tts_sherpa_dir,
        choose_string_param(
            node, overrides,
            "tts.local.sherpa_onnx.dict_dir",
            "tts.local.sherpa.dict_dir"));
    cfg.tts_local_sherpa.data_dir = resolve_path(
        cfg.models_dir, tts_sherpa_dir,
        choose_string_param(
            node, overrides,
            "tts.local.sherpa_onnx.data_dir",
            "tts.local.sherpa.data_dir"));
    cfg.tts_local_sherpa.rule_fsts =
        resolve_path_list(
            cfg.models_dir, tts_sherpa_dir,
            choose_string_param(
                node, overrides,
                "tts.local.sherpa_onnx.rule_fsts",
                "tts.local.sherpa.rule_fsts"));
    cfg.tts_local_sherpa.sid = choose_int_param(
        node, overrides,
        "tts.local.sherpa_onnx.sid",
        "tts.local.sherpa.sid");
    cfg.tts_local_sherpa.speed = choose_float_param(
        node, overrides,
        "tts.local.sherpa_onnx.speed",
        "tts.local.sherpa.speed");

    cfg.tts_local_melo.models_dir = cfg.models_dir;
    cfg.tts_local_melo.sample_rate = cfg.sample_rate;
    cfg.tts_local_melo.model_dir = choose_string_param(
        node, overrides,
        "tts.local.melo_rknn.model_dir",
        "tts.local.melo.model_dir");
    cfg.tts_local_melo.melo_speaker = choose_string_param(
        node, overrides,
        "tts.local.melo_rknn.speaker",
        "tts.local.melo.speaker");
    cfg.tts_local_melo.melo_rank = choose_string_param(
        node, overrides,
        "tts.local.melo_rknn.rank",
        "tts.local.melo.rank");
    cfg.tts_local_melo.melo_bert_mode = choose_string_param(
        node, overrides,
        "tts.local.melo_rknn.bert_mode",
        "tts.local.melo.bert_mode");
    cfg.tts_local_melo.melo_speed = choose_float_param(
        node, overrides,
        "tts.local.melo_rknn.speed",
        "tts.local.melo.speed");
    cfg.tts_local_melo.melo_sdp_ratio = choose_float_param(
        node, overrides,
        "tts.local.melo_rknn.sdp_ratio",
        "tts.local.melo.sdp_ratio");
    cfg.tts_local_melo.melo_noise_scale = choose_float_param(
        node, overrides,
        "tts.local.melo_rknn.noise_scale",
        "tts.local.melo.noise_scale");
    cfg.tts_local_melo.melo_noise_scale_w = choose_float_param(
        node, overrides,
        "tts.local.melo_rknn.noise_scale_w",
        "tts.local.melo.noise_scale_w");

    cfg.tts_local_moss.models_dir = cfg.models_dir;
    cfg.tts_local_moss.sample_rate = cfg.sample_rate;
    cfg.tts_local_moss.model_dir = choose_string_param(
        node, overrides,
        "tts.local.moss_onnx.model_dir",
        "tts.local.moss.model_dir");
    cfg.tts_local_moss.voice = choose_string_param(
        node, overrides,
        "tts.local.moss_onnx.voice",
        "tts.local.moss.voice");
    cfg.tts_local_moss.max_new_frames = choose_int_param(
        node, overrides,
        "tts.local.moss_onnx.max_new_frames",
        "tts.local.moss.max_new_frames");
    cfg.tts_local_moss.seed = choose_int_param(
        node, overrides,
        "tts.local.moss_onnx.seed",
        "tts.local.moss.seed");

    return cfg;
}
