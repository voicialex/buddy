#include "buddy_audio/audio_pipeline_node.hpp"

#include <dlfcn.h>

#include <filesystem>
#include <sstream>

#include "buddy_audio/asr_backend.hpp"
#include "buddy_audio/tts_backend.hpp"

namespace {

std::string resolve_path(const std::string& models_dir, const std::string& model_dir, const std::string& filename) {
    if (filename.empty()) {
        return "";
    }
    // If filename is already an absolute path, return as-is
    if (!filename.empty() && filename[0] == '/') {
        return filename;
    }
    return std::filesystem::path(models_dir) / model_dir / filename;
}

// Resolve a comma-separated list of paths (e.g. rule_fsts)
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

}  // namespace

AudioPipelineNode::AudioPipelineNode(const rclcpp::NodeOptions& options)
    : rclcpp_lifecycle::LifecycleNode("audio", options) {}

AudioPipelineNode::~AudioPipelineNode() {
    running_ = false;
    if (capture_thread_.joinable()) {
        capture_thread_.join();
    }
}

CallbackReturn AudioPipelineNode::on_configure(const rclcpp_lifecycle::State&) {
    RCLCPP_INFO(get_logger(), "AudioPipelineNode: configuring");

    // Declare parameters
    declare_parameter("models_dir", "");
    declare_parameter("mic_device", "default");
    declare_parameter("speaker_device", "default");
    declare_parameter("sample_rate", 16000);
    declare_parameter("kws.enable", true);
    declare_parameter("kws.model_dir", "");
    declare_parameter("kws.model.encoder", "");
    declare_parameter("kws.model.decoder", "");
    declare_parameter("kws.model.joiner", "");
    declare_parameter("kws.model.tokens", "");
    declare_parameter("kws.keywords_file", "");
    declare_parameter("kws.threshold", 0.1f);
    declare_parameter("kws.score", 3.0f);
    declare_parameter("asr.mode", "local");
    declare_parameter("asr.engine", "auto");
    declare_parameter("asr.runtime", "auto");
    declare_parameter("asr.local.sherpa.model_dir", "");
    declare_parameter("asr.local.sherpa.model.encoder", "");
    declare_parameter("asr.local.sherpa.model.decoder", "");
    declare_parameter("asr.local.sherpa.model.joiner", "");
    declare_parameter("asr.local.sherpa.model.tokens", "");
    declare_parameter("asr.local.sherpa.decoding_method", "greedy_search");
    declare_parameter("asr.local.native.model_dir", "");
    declare_parameter("asr.local.native.model.encoder", "");
    declare_parameter("asr.local.native.model.decoder", "");
    declare_parameter("asr.local.native.model.joiner", "");
    declare_parameter("asr.local.native.model.tokens", "");
    declare_parameter("asr.server.funasr.url", "ws://127.0.0.1:10095");
    declare_parameter("asr.server.funasr.mode", "online");
    declare_parameter("tts.mode", "local");
    declare_parameter("tts.engine", "auto");
    declare_parameter("tts.runtime", "auto");
    declare_parameter("tts.server.chattts.url", "http://127.0.0.1:9880/tts");
    declare_parameter("tts.local.sherpa.model_type", "kokoro");
    declare_parameter("tts.local.sherpa.model_dir", "");
    declare_parameter("tts.local.sherpa.model", "");
    declare_parameter("tts.local.sherpa.tokens", "");
    declare_parameter("tts.local.sherpa.voices", "");
    declare_parameter("tts.local.sherpa.lexicon", "");
    declare_parameter("tts.local.sherpa.lang", "");
    declare_parameter("tts.local.sherpa.dict_dir", "");
    declare_parameter("tts.local.sherpa.data_dir", "");
    declare_parameter("tts.local.sherpa.rule_fsts", "");
    declare_parameter("tts.local.sherpa.sid", 0);
    declare_parameter("tts.local.sherpa.speed", 1.0);
    declare_parameter("tts.local.moss.model_dir", "");
    declare_parameter("tts.local.moss.voice", "");
    declare_parameter("tts.local.moss.max_new_frames", 375);
    declare_parameter("tts.local.moss.seed", 1234);
    declare_parameter("asr_cooldown_ms", 500);
    declare_parameter("asr_wake_guard_ms", 700);

    auto mic_device = get_parameter("mic_device").as_string();
    auto speaker_device = get_parameter("speaker_device").as_string();
    sample_rate_ = get_parameter("sample_rate").as_int();
    kws_enabled_ = get_parameter("kws.enable").as_bool();
    // Cooldown: convert ms to chunk count (1 chunk = 100ms at 16kHz, chunk=1600)
    int cooldown_ms = get_parameter("asr_cooldown_ms").as_int();
    asr_cooldown_chunks_ = std::max(1, cooldown_ms / 100);
    // Ignore ASR for a short window right after wake word to avoid tail pickup (e.g. "迪").
    int wake_guard_ms = get_parameter("asr_wake_guard_ms").as_int();
    asr_wake_guard_chunks_ = std::max(0, wake_guard_ms / 100);
    auto models_dir = get_parameter("models_dir").as_string();

    // AudioCapture
    capture_ = std::make_unique<AudioCapture>(get_logger());
    capture_->configure(mic_device, sample_rate_);

    // SpeechRecognizer
    auto kws_model_dir = get_parameter("kws.model_dir").as_string();
    KwsConfig kws_cfg;
    kws_cfg.encoder = resolve_path(models_dir, kws_model_dir, get_parameter("kws.model.encoder").as_string());
    kws_cfg.decoder = resolve_path(models_dir, kws_model_dir, get_parameter("kws.model.decoder").as_string());
    kws_cfg.joiner = resolve_path(models_dir, kws_model_dir, get_parameter("kws.model.joiner").as_string());
    kws_cfg.tokens = resolve_path(models_dir, kws_model_dir, get_parameter("kws.model.tokens").as_string());
    kws_cfg.keywords_file = resolve_path(models_dir, kws_model_dir, get_parameter("kws.keywords_file").as_string());
    kws_cfg.threshold = static_cast<float>(get_parameter("kws.threshold").as_double());
    kws_cfg.score = static_cast<float>(get_parameter("kws.score").as_double());

    // ASR backend
    auto asr_mode = get_parameter("asr.mode").as_string();
    auto asr_engine = get_parameter("asr.engine").as_string();
    auto asr_runtime = get_parameter("asr.runtime").as_string();

    // Auto-detection: probe librknnrt.so via dlopen
    if (asr_runtime == "auto") {
#ifdef HAS_RKNN
        void* rknn_handle = dlopen("librknnrt.so", RTLD_LAZY | RTLD_NOLOAD);
        if (!rknn_handle) rknn_handle = dlopen("librknnrt.so", RTLD_LAZY);
        if (rknn_handle) {
            asr_runtime = "rknnruntime";
            dlclose(rknn_handle);
            RCLCPP_INFO(get_logger(), "RKNN runtime detected, using NPU for ASR");
        } else
#endif
        {
            asr_runtime = "onnxruntime";
        }
    }
    if (asr_engine == "auto") {
        asr_engine = (asr_runtime == "rknnruntime") ? "native" : "sherpa-onnx";
    }

    auto asr_backend = create_asr_backend(asr_mode, asr_engine, asr_runtime);
    if (!asr_backend) {
        RCLCPP_ERROR(get_logger(), "Unknown ASR mode=%s engine=%s", asr_mode.c_str(), asr_engine.c_str());
        return CallbackReturn::FAILURE;
    }

    AsrBackendConfig asr_cfg;
    asr_cfg.models_dir = models_dir;
    asr_cfg.sample_rate = sample_rate_;
    asr_cfg.runtime = asr_runtime;
    if (asr_mode == "local" && asr_engine == "native") {
        asr_cfg.model_dir = get_parameter("asr.local.native.model_dir").as_string();
        asr_cfg.encoder = get_parameter("asr.local.native.model.encoder").as_string();
        asr_cfg.decoder = get_parameter("asr.local.native.model.decoder").as_string();
        asr_cfg.joiner = get_parameter("asr.local.native.model.joiner").as_string();
        asr_cfg.tokens = get_parameter("asr.local.native.model.tokens").as_string();
    } else if (asr_mode == "local") {
        asr_cfg.model_dir = get_parameter("asr.local.sherpa.model_dir").as_string();
        asr_cfg.encoder = get_parameter("asr.local.sherpa.model.encoder").as_string();
        asr_cfg.decoder = get_parameter("asr.local.sherpa.model.decoder").as_string();
        asr_cfg.joiner = get_parameter("asr.local.sherpa.model.joiner").as_string();
        asr_cfg.tokens = get_parameter("asr.local.sherpa.model.tokens").as_string();
        asr_cfg.decoding_method = get_parameter("asr.local.sherpa.decoding_method").as_string();
    } else {
        asr_cfg.server_url = get_parameter("asr.server.funasr.url").as_string();
        asr_cfg.server_mode = get_parameter("asr.server.funasr.mode").as_string();
    }

    RCLCPP_INFO(get_logger(), "ASR: mode=%s engine=%s runtime=%s",
                asr_mode.c_str(), asr_engine.c_str(), asr_runtime.c_str());

    if (!asr_backend->initialize(asr_cfg, get_logger())) {
        RCLCPP_ERROR(get_logger(), "Failed to initialize ASR backend (mode=%s)", asr_mode.c_str());
        return CallbackReturn::FAILURE;
    }

    recognizer_ = std::make_unique<SpeechRecognizer>(get_logger());
    if (!recognizer_->configure(sample_rate_, kws_enabled_, kws_cfg, std::move(asr_backend))) {
        return CallbackReturn::FAILURE;
    }

    // TtsPlayer
    auto tts_mode = get_parameter("tts.mode").as_string();
    auto tts_engine = get_parameter("tts.engine").as_string();
    auto tts_runtime = get_parameter("tts.runtime").as_string();

    // Auto-detection: TTS has no RKNN backend yet, always use onnxruntime
    if (tts_runtime == "auto") {
        tts_runtime = "onnxruntime";
    }
    if (tts_engine == "auto") {
        tts_engine = (tts_runtime == "rknnruntime") ? "native" : "sherpa-onnx";
    }

    if (!tts_mode.empty()) {
        // Build TtsBackendConfig
        TtsBackendConfig tts_cfg;
        tts_cfg.models_dir = models_dir;
        tts_cfg.sample_rate = sample_rate_;
        tts_cfg.runtime = tts_runtime;
        if (tts_mode == "server") {
            tts_cfg.server_url = get_parameter("tts.server.chattts.url").as_string();
        } else if (tts_engine == "sherpa-onnx" || tts_engine == "sherpa") {
            auto tts_model_dir = get_parameter("tts.local.sherpa.model_dir").as_string();
            tts_cfg.model_dir = tts_model_dir;
            tts_cfg.model_type = get_parameter("tts.local.sherpa.model_type").as_string();
            tts_cfg.model = resolve_path(models_dir, tts_model_dir, get_parameter("tts.local.sherpa.model").as_string());
            tts_cfg.tokens = resolve_path(models_dir, tts_model_dir, get_parameter("tts.local.sherpa.tokens").as_string());
            tts_cfg.voices = resolve_path(models_dir, tts_model_dir, get_parameter("tts.local.sherpa.voices").as_string());
            tts_cfg.lexicon = resolve_path(models_dir, tts_model_dir, get_parameter("tts.local.sherpa.lexicon").as_string());
            tts_cfg.lang = get_parameter("tts.local.sherpa.lang").as_string();
            tts_cfg.dict_dir = resolve_path(models_dir, tts_model_dir, get_parameter("tts.local.sherpa.dict_dir").as_string());
            tts_cfg.data_dir = resolve_path(models_dir, tts_model_dir, get_parameter("tts.local.sherpa.data_dir").as_string());
            tts_cfg.rule_fsts = resolve_path_list(models_dir, tts_model_dir, get_parameter("tts.local.sherpa.rule_fsts").as_string());
            tts_cfg.sid = static_cast<int>(get_parameter("tts.local.sherpa.sid").as_int());
            tts_cfg.speed = static_cast<float>(get_parameter("tts.local.sherpa.speed").as_double());
        } else if (tts_engine == "native" || tts_engine == "moss") {
            tts_cfg.model_dir = get_parameter("tts.local.moss.model_dir").as_string();
            tts_cfg.voice = get_parameter("tts.local.moss.voice").as_string();
            tts_cfg.max_new_frames = static_cast<int>(get_parameter("tts.local.moss.max_new_frames").as_int());
            tts_cfg.seed = static_cast<int>(get_parameter("tts.local.moss.seed").as_int());
        }

        auto backend = create_tts_backend(tts_mode, tts_engine, tts_runtime);
        if (!backend || !backend->initialize(tts_cfg, get_logger())) {
            RCLCPP_ERROR(get_logger(), "Failed to create TTS backend (mode=%s, engine=%s)", tts_mode.c_str(), tts_engine.c_str());
            return CallbackReturn::FAILURE;
        }

        tts_player_ = std::make_unique<TtsPlayer>(get_logger());
        tts_player_->configure(std::move(backend), speaker_device, [this]() {
            // Don't resume ASR immediately — start cooldown countdown.
            // capture_loop will unpause ASR and publish tts_done after cooldown.
            cooldown_chunks_remaining_.store(asr_cooldown_chunks_);
        });
        RCLCPP_INFO(get_logger(), "TTS ready (mode=%s, engine=%s, runtime=%s)",
                    tts_mode.c_str(), tts_engine.c_str(), tts_runtime.c_str());
    }

    // ROS interfaces
    wake_word_pub_ = create_publisher<std_msgs::msg::String>("/audio/wake_word", 10);
    asr_text_pub_ = create_publisher<std_msgs::msg::String>("/audio/asr_text", 10);
    tts_done_pub_ = create_publisher<std_msgs::msg::Empty>("/audio/tts_done", 10);
    sentence_sub_ = create_subscription<buddy_interfaces::msg::Sentence>(
        "/brain/sentence", 10, std::bind(&AudioPipelineNode::on_sentence, this, std::placeholders::_1));
    tts_control_sub_ = create_subscription<buddy_interfaces::msg::TtsControl>(
        "/brain/tts_control", 10, std::bind(&AudioPipelineNode::on_tts_control, this, std::placeholders::_1));

    RCLCPP_INFO(get_logger(),
                "AudioPipelineNode: configured (KWS=%s, TTS=%s)",
                kws_enabled_ ? "on" : "off",
                tts_player_ ? "on" : "off");
    return CallbackReturn::SUCCESS;
}

CallbackReturn AudioPipelineNode::on_activate(const rclcpp_lifecycle::State&) {
    RCLCPP_INFO(get_logger(), "AudioPipelineNode: activating");
    running_ = true;
    // Keep ASR running from startup; brain node will gate first-turn requests.
    recognizer_->pause_asr(false);
    capture_thread_ = std::thread(&AudioPipelineNode::capture_loop, this);
    if (tts_player_) {
        tts_player_->start();
    }
    if (!kws_enabled_) {
        auto msg = std_msgs::msg::String();
        msg.data = "__no_kws__";
        wake_word_pub_->publish(msg);
        RCLCPP_INFO(get_logger(), "KWS disabled, auto-triggered wake_word");
    }
    return CallbackReturn::SUCCESS;
}

void AudioPipelineNode::capture_loop() {
    while (running_) {
        auto chunk = capture_->read_chunk();
        if (chunk.empty()) {
            continue;
        }

        // Handle cooldown countdown after TTS done
        if (cooldown_chunks_remaining_ > 0) {
            cooldown_chunks_remaining_--;
            if (cooldown_chunks_remaining_ == 0) {
                recognizer_->reset_asr();
                // Resume ASR after TTS cooldown so user can continue within session without wake word.
                recognizer_->pause_asr(false);
                std_msgs::msg::Empty done;
                tts_done_pub_->publish(done);
            }
        }
        if (wake_guard_chunks_remaining_ > 0) {
            wake_guard_chunks_remaining_--;
        }

        auto event = recognizer_->feed(chunk.data(), static_cast<int>(chunk.size()));
        switch (event.type) {
            case RecognitionEvent::WAKE_WORD: {
                RCLCPP_INFO(get_logger(), "KWS detected: %s", event.text.c_str());
                cooldown_chunks_remaining_ = 0;
                wake_guard_chunks_remaining_ = asr_wake_guard_chunks_;
                recognizer_->pause_asr(false);
                recognizer_->reset_asr();
                auto msg = std::make_unique<std_msgs::msg::String>();
                msg->data = event.text;
                wake_word_pub_->publish(std::move(msg));
                break;
            }
            case RecognitionEvent::ASR_TEXT: {
                if (wake_guard_chunks_remaining_ > 0) {
                    RCLCPP_INFO(get_logger(), "ASR ignored during wake guard: %s", event.text.c_str());
                    recognizer_->reset_asr();
                    break;
                }
                RCLCPP_INFO(get_logger(), "ASR result: %s", event.text.c_str());
                auto msg = std::make_unique<std_msgs::msg::String>();
                msg->data = event.text;
                asr_text_pub_->publish(std::move(msg));
                break;
            }
            case RecognitionEvent::NONE:
                break;
        }
    }
}

void AudioPipelineNode::on_sentence(const buddy_interfaces::msg::Sentence& msg) {
    RCLCPP_INFO(get_logger(),
                "TTS: sentence [%u] turn=%s is_final=%d: %s",
                msg.index,
                msg.turn_id.c_str(),
                msg.is_final,
                msg.text.c_str());

    // New turn → clear stale queue
    if (!msg.turn_id.empty() && msg.turn_id != current_turn_id_) {
        if (tts_player_) {
            tts_player_->clear_queue();
        }
        current_turn_id_ = msg.turn_id;
        RCLCPP_INFO(get_logger(), "New turn %s, TTS queue cleared", msg.turn_id.c_str());
    }

    if (tts_player_) {
        if (!msg.text.empty()) {
            recognizer_->pause_asr(true);
        }
        tts_player_->enqueue(msg);
    } else {
        if (msg.is_final) {
            std_msgs::msg::Empty done;
            tts_done_pub_->publish(done);
        }
    }
}

void AudioPipelineNode::on_tts_control(const buddy_interfaces::msg::TtsControl& msg) {
    if (!tts_player_) {
        return;
    }
    if (msg.command != buddy_interfaces::msg::TtsControl::REPLACE_NOW) {
        return;
    }
    if (!current_turn_id_.empty() && !msg.turn_id.empty() && msg.turn_id != current_turn_id_) {
        return;
    }
    tts_player_->interrupt_now();
}

CallbackReturn AudioPipelineNode::on_deactivate(const rclcpp_lifecycle::State&) {
    RCLCPP_INFO(get_logger(), "AudioPipelineNode: deactivating");
    running_ = false;
    if (tts_player_) {
        tts_player_->stop();
    }
    if (capture_thread_.joinable()) {
        capture_thread_.join();
    }
    return CallbackReturn::SUCCESS;
}

CallbackReturn AudioPipelineNode::on_cleanup(const rclcpp_lifecycle::State&) {
    RCLCPP_INFO(get_logger(), "AudioPipelineNode: cleaning up");
    recognizer_->cleanup();
    tts_player_.reset();
    capture_.reset();
    recognizer_.reset();
    wake_word_pub_.reset();
    asr_text_pub_.reset();
    tts_done_pub_.reset();
    sentence_sub_.reset();
    tts_control_sub_.reset();
    return CallbackReturn::SUCCESS;
}

CallbackReturn AudioPipelineNode::on_shutdown(const rclcpp_lifecycle::State&) {
    RCLCPP_INFO(get_logger(), "AudioPipelineNode: shutting down");
    return CallbackReturn::SUCCESS;
}

CallbackReturn AudioPipelineNode::on_error(const rclcpp_lifecycle::State&) {
    RCLCPP_ERROR(get_logger(), "AudioPipelineNode: error");
    return CallbackReturn::SUCCESS;
}

#include <rclcpp_components/register_node_macro.hpp>
RCLCPP_COMPONENTS_REGISTER_NODE(AudioPipelineNode)
