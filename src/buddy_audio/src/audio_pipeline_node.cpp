#include "buddy_audio/audio_pipeline_node.hpp"

#include "buddy_audio/audio_config.hpp"
#include "buddy_audio/engine_factory.hpp"

#include <cmath>

namespace {

float rms_of(const std::vector<float>& samples) {
    if (samples.empty()) {
        return 0.0f;
    }
    double sum = 0.0;
    for (float v : samples) {
        sum += static_cast<double>(v) * static_cast<double>(v);
    }
    return static_cast<float>(std::sqrt(sum / static_cast<double>(samples.size())));
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

    declare_audio_parameters(*this);
    const AudioConfig config = load_audio_config(*this);

    sample_rate_ = config.sample_rate;
    kws_enabled_ = config.kws_enabled;
    asr_cooldown_chunks_ = config.asr_cooldown_chunks;
    asr_wake_guard_chunks_ = config.asr_wake_guard_chunks;
    vad_publish_enabled_ = config.preprocess.enable && config.preprocess.vad_enable;

    // AudioCapture
    capture_ = std::make_unique<AudioCapture>(get_logger());
    capture_->configure(config.mic_device, sample_rate_);
    preprocessor_ = std::make_unique<AudioPreprocessor>(get_logger(), sample_rate_, config.preprocess);
    RCLCPP_INFO(get_logger(), "Audio preprocessor: %s",
                preprocessor_->is_enabled() ? "enabled" : "disabled");

    AsrEngineBundle asr_bundle;
    if (!create_asr_engine(config, get_logger(), &asr_bundle)) {
        RCLCPP_ERROR(get_logger(), "Failed to initialize ASR backend");
        return CallbackReturn::FAILURE;
    }
    RCLCPP_INFO(get_logger(), "ASR: mode=%s engine=%s runtime=%s",
                asr_bundle.mode.c_str(), asr_bundle.engine.c_str(), asr_bundle.runtime.c_str());

    recognizer_ = std::make_unique<SpeechRecognizer>(get_logger());
    if (!recognizer_->configure(sample_rate_, kws_enabled_, config.kws, std::move(asr_bundle.backend))) {
        return CallbackReturn::FAILURE;
    }

    if (!config.tts_mode.empty()) {
        std::unique_ptr<TtsBackend> tts_backend;
        std::string tts_mode;
        std::string tts_engine;
        std::string tts_runtime;
        if (!create_tts_engine(config, get_logger(), &tts_backend, &tts_mode, &tts_engine, &tts_runtime)) {
            RCLCPP_ERROR(get_logger(), "Failed to create TTS backend (mode=%s, engine=%s)", config.tts_mode.c_str(),
                         config.tts_engine.c_str());
            return CallbackReturn::FAILURE;
        }

        tts_player_ = std::make_unique<TtsPlayer>(get_logger());
        tts_player_->configure(
            std::move(tts_backend),
            config.speaker_device,
            [this]() {
                // Don't resume ASR immediately — start cooldown countdown.
                // capture_loop will unpause ASR and publish tts_done after cooldown.
                cooldown_chunks_remaining_.store(asr_cooldown_chunks_);
            },
            [this](const float* samples, int32_t n, int32_t sample_rate) {
                if (preprocessor_) {
                    preprocessor_->push_render_chunk(samples, n, sample_rate);
                }
            });
        RCLCPP_INFO(get_logger(), "TTS ready (mode=%s, engine=%s, runtime=%s)",
                    tts_mode.c_str(), tts_engine.c_str(), tts_runtime.c_str());
    }

    // ROS interfaces
    wake_word_pub_ = create_publisher<std_msgs::msg::String>("/audio/wake_word", 10);
    asr_text_pub_ = create_publisher<std_msgs::msg::String>("/audio/asr_text", 10);
    tts_done_pub_ = create_publisher<std_msgs::msg::Empty>("/audio/tts_done", 10);
    voice_activity_pub_ = create_publisher<std_msgs::msg::Bool>("/audio/voice_activity", 10);
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
    int diag_chunks = 0;
    while (running_) {
        auto chunk = capture_->read_chunk();
        if (chunk.empty()) {
            continue;
        }

        const float raw_rms = rms_of(chunk);

        AudioPreprocessResult preprocess;
        if (preprocessor_ && preprocessor_->is_enabled()) {
            preprocess = preprocessor_->process_capture_chunk(&chunk, kws_enabled_);
            const float post_rms = rms_of(chunk);
            if (vad_publish_enabled_ && voice_activity_pub_) {
                auto vad_msg = std_msgs::msg::Bool();
                vad_msg.data = preprocess.has_voice && post_rms > 0.002f;
                voice_activity_pub_->publish(vad_msg);
            }

            // Periodic diag: raw/post RMS + voice + pass (every 10 chunks / 1s)
            if (++diag_chunks % 10 == 0) {
                RCLCPP_INFO(get_logger(),
                    "DIAG: raw_rms=%.5f post_rms=%.5f voice=%d pass=%d cooldown=%d wguard=%d",
                    raw_rms, post_rms,
                    preprocess.has_voice ? 1 : 0,
                    preprocess.should_pass ? 1 : 0,
                    cooldown_chunks_remaining_.load(),
                    wake_guard_chunks_remaining_.load());
            }

            if (!preprocess.should_pass) {
                continue;
            }
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
    // turn_id 格式 sess-<ts>-t<N>，日志只打印 t<N> 部分
    std::string short_turn = msg.turn_id;
    if (auto pos = short_turn.rfind('-'); pos != std::string::npos) {
        short_turn = short_turn.substr(pos + 1);
    }

    RCLCPP_INFO(get_logger(),
                "TTS: sentence [%u] turn=%s is_final=%d: %s",
                msg.index,
                short_turn.c_str(),
                msg.is_final,
                msg.text.c_str());

    // New turn → clear stale queue
    if (!msg.turn_id.empty() && msg.turn_id != current_turn_id_) {
        if (tts_player_) {
            tts_player_->clear_queue();
            tts_player_->clear_interrupt();
        }
        current_turn_id_ = msg.turn_id;
        RCLCPP_INFO(get_logger(), "New turn %s, TTS queue cleared", short_turn.c_str());
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
    // Start cooldown directly — on_done_ may never fire if clear_queue drops the
    // empty final Sentence that Brain publishes right after TtsControl.
    cooldown_chunks_remaining_.store(asr_cooldown_chunks_);
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
    preprocessor_.reset();
    recognizer_.reset();
    wake_word_pub_.reset();
    asr_text_pub_.reset();
    tts_done_pub_.reset();
    voice_activity_pub_.reset();
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
