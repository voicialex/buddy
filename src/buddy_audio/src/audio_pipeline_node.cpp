#include "buddy_audio/audio_pipeline_node.hpp"

#include <vector>

AudioPipelineNode::AudioPipelineNode(const rclcpp::NodeOptions &options)
    : rclcpp_lifecycle::LifecycleNode("audio", options) {}

void AudioPipelineNode::setup_transducer_config(
    SherpaOnnxOnlineTransducerModelConfig &transducer,
    SherpaOnnxOnlineModelConfig &model_cfg, SherpaOnnxFeatureConfig &feat_cfg,
    int sample_rate, const std::string &encoder, const std::string &decoder,
    const std::string &joiner, const std::string &tokens) {
  feat_cfg.sample_rate = sample_rate;
  feat_cfg.feature_dim = 80;
  transducer.encoder = encoder.c_str();
  transducer.decoder = decoder.c_str();
  transducer.joiner = joiner.c_str();
  model_cfg.tokens = tokens.c_str();
  model_cfg.provider = "cpu";
  model_cfg.num_threads = 1;
}

CallbackReturn
AudioPipelineNode::on_configure(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "AudioPipelineNode: configuring");

  // Parameters
  declare_parameter("mic_device", "default");
  declare_parameter("speaker_device", "default");
  declare_parameter("sample_rate", 16000);
  declare_parameter("kws.enable", true);
  declare_parameter("kws.model.encoder", "");
  declare_parameter("kws.model.decoder", "");
  declare_parameter("kws.model.joiner", "");
  declare_parameter("kws.model.tokens", "");
  declare_parameter("kws.keywords_file", "");
  declare_parameter("kws.threshold", 0.1f);
  declare_parameter("kws.score", 3.0f);
  declare_parameter("asr.model.encoder", "");
  declare_parameter("asr.model.decoder", "");
  declare_parameter("asr.model.joiner", "");
  declare_parameter("asr.model.tokens", "");
  declare_parameter("asr.decoding_method", "greedy_search");

  mic_device_ = get_parameter("mic_device").as_string();
  speaker_device_ = get_parameter("speaker_device").as_string();
  sample_rate_ = get_parameter("sample_rate").as_int();
  kws_enabled_ = get_parameter("kws.enable").as_bool();
  RCLCPP_INFO(get_logger(), "Audio devices: mic=%s, speaker=%s, kws=%s",
              mic_device_.c_str(), speaker_device_.c_str(),
              kws_enabled_ ? "on" : "off");

  // --- KWS init (optional) ---
  if (kws_enabled_) {
    auto kws_encoder = get_parameter("kws.model.encoder").as_string();
    auto kws_decoder = get_parameter("kws.model.decoder").as_string();
    auto kws_joiner = get_parameter("kws.model.joiner").as_string();
    auto kws_tokens = get_parameter("kws.model.tokens").as_string();
    auto kws_keywords = get_parameter("kws.keywords_file").as_string();
    auto kws_threshold = get_parameter("kws.threshold").as_double();
    auto kws_score = get_parameter("kws.score").as_double();

    if (kws_encoder.empty()) {
      RCLCPP_ERROR(get_logger(), "KWS model encoder path not set");
      return CallbackReturn::FAILURE;
    }

    SherpaOnnxKeywordSpotterConfig kws_cfg{};
    setup_transducer_config(kws_cfg.model_config.transducer,
                           kws_cfg.model_config, kws_cfg.feat_config,
                           sample_rate_, kws_encoder, kws_decoder, kws_joiner,
                           kws_tokens);
    kws_cfg.keywords_file = kws_keywords.c_str();
    kws_cfg.keywords_threshold = static_cast<float>(kws_threshold);
    kws_cfg.keywords_score = static_cast<float>(kws_score);
    kws_cfg.max_active_paths = 4;

    kws_ = SherpaOnnxCreateKeywordSpotter(&kws_cfg);
    if (!kws_) {
      RCLCPP_ERROR(get_logger(), "Failed to create KWS");
      return CallbackReturn::FAILURE;
    }
    kws_stream_ = SherpaOnnxCreateKeywordStream(kws_);
  }

  // --- ASR init ---
  auto asr_encoder = get_parameter("asr.model.encoder").as_string();
  auto asr_decoder = get_parameter("asr.model.decoder").as_string();
  auto asr_joiner = get_parameter("asr.model.joiner").as_string();
  auto asr_tokens = get_parameter("asr.model.tokens").as_string();
  auto asr_method = get_parameter("asr.decoding_method").as_string();

  SherpaOnnxOnlineRecognizerConfig asr_cfg{};
  setup_transducer_config(asr_cfg.model_config.transducer, asr_cfg.model_config,
                          asr_cfg.feat_config, sample_rate_, asr_encoder,
                          asr_decoder, asr_joiner, asr_tokens);
  asr_cfg.decoding_method = asr_method.c_str();
  asr_cfg.enable_endpoint = 1;
  asr_cfg.rule1_min_trailing_silence = 2.4f;
  asr_cfg.rule2_min_trailing_silence = 1.2f;
  asr_cfg.rule3_min_utterance_length = 20.0f;

  asr_ = SherpaOnnxCreateOnlineRecognizer(&asr_cfg);
  if (!asr_) {
    RCLCPP_ERROR(get_logger(), "Failed to create ASR");
    return CallbackReturn::FAILURE;
  }
  asr_stream_ = SherpaOnnxCreateOnlineStream(asr_);

  wake_word_pub_ =
      create_publisher<std_msgs::msg::String>("/audio/wake_word", 10);
  asr_text_pub_ =
      create_publisher<std_msgs::msg::String>("/audio/asr_text", 10);
  tts_done_pub_ = create_publisher<std_msgs::msg::Empty>("/audio/tts_done", 10);
  sentence_sub_ = create_subscription<buddy_interfaces::msg::Sentence>(
      "/brain/sentence", 10,
      std::bind(&AudioPipelineNode::on_sentence, this, std::placeholders::_1));

  RCLCPP_INFO(get_logger(), "AudioPipelineNode: configured (KWS=%s, ASR ready)",
              kws_enabled_ ? "on" : "off");
  return CallbackReturn::SUCCESS;
}

CallbackReturn AudioPipelineNode::on_activate(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "AudioPipelineNode: activating");

  int rc = snd_pcm_open(&pcm_, mic_device_.c_str(), SND_PCM_STREAM_CAPTURE, 0);
  if (rc < 0) {
    RCLCPP_ERROR(get_logger(), "ALSA open failed: %s", snd_strerror(rc));
    return CallbackReturn::FAILURE;
  }

  snd_pcm_hw_params_t *hw;
  snd_pcm_hw_params_alloca(&hw);
  snd_pcm_hw_params_any(pcm_, hw);
  snd_pcm_hw_params_set_access(pcm_, hw, SND_PCM_ACCESS_RW_INTERLEAVED);
  snd_pcm_hw_params_set_format(pcm_, hw, SND_PCM_FORMAT_S16_LE);
  snd_pcm_hw_params_set_channels(pcm_, hw, 1);
  unsigned int rate = static_cast<unsigned int>(sample_rate_);
  snd_pcm_hw_params_set_rate_near(pcm_, hw, &rate, nullptr);
  snd_pcm_uframes_t period = 1600;
  snd_pcm_hw_params_set_period_size_near(pcm_, hw, &period, nullptr);
  rc = snd_pcm_hw_params(pcm_, hw);
  if (rc < 0) {
    RCLCPP_ERROR(get_logger(), "ALSA hw_params failed: %s", snd_strerror(rc));
    snd_pcm_close(pcm_);
    pcm_ = nullptr;
    return CallbackReturn::FAILURE;
  }

  running_ = true;
  mode_ = kws_enabled_ ? Mode::KWS : Mode::ASR;
  capture_thread_ = std::thread(&AudioPipelineNode::capture_loop, this);

  RCLCPP_INFO(get_logger(), "AudioPipelineNode: activated (mode=%s)",
              kws_enabled_ ? "KWS" : "ASR-only");
  return CallbackReturn::SUCCESS;
}

void AudioPipelineNode::capture_loop() {
  const int chunk = 1600; // 100ms at 16kHz
  std::vector<int16_t> buf(chunk);
  std::vector<float> fbuf(chunk);

  while (running_) {
    snd_pcm_sframes_t n = snd_pcm_readi(pcm_, buf.data(), chunk);
    if (n < 0) {
      n = snd_pcm_recover(pcm_, static_cast<int>(n), 0);
      if (n < 0) {
        RCLCPP_ERROR(get_logger(), "ALSA read error: %s", snd_strerror(n));
        break;
      }
    }

    for (snd_pcm_sframes_t i = 0; i < n; ++i) {
      fbuf[static_cast<size_t>(i)] = buf[static_cast<size_t>(i)] / 32768.0f;
    }

    if (mode_ == Mode::KWS) {
      SherpaOnnxOnlineStreamAcceptWaveform(kws_stream_, sample_rate_,
                                           fbuf.data(), static_cast<int>(n));
      while (SherpaOnnxIsKeywordStreamReady(kws_, kws_stream_)) {
        SherpaOnnxDecodeKeywordStream(kws_, kws_stream_);
      }
      const SherpaOnnxKeywordResult *r =
          SherpaOnnxGetKeywordResult(kws_, kws_stream_);
      if (r && r->keyword && r->keyword[0] != '\0') {
        RCLCPP_INFO(get_logger(), "KWS detected: %s", r->keyword);
        auto msg = std::make_unique<std_msgs::msg::String>();
        msg->data = r->keyword;
        wake_word_pub_->publish(std::move(msg));
        SherpaOnnxDestroyKeywordResult(r);
        // Reset KWS stream for next detection
        SherpaOnnxDestroyOnlineStream(kws_stream_);
        kws_stream_ = SherpaOnnxCreateKeywordStream(kws_);
        mode_ = Mode::ASR;
        continue;
      }
      if (r) {
        SherpaOnnxDestroyKeywordResult(r);
      }
    } else {
      SherpaOnnxOnlineStreamAcceptWaveform(asr_stream_, sample_rate_,
                                           fbuf.data(), static_cast<int>(n));
      while (SherpaOnnxIsOnlineStreamReady(asr_, asr_stream_)) {
        SherpaOnnxDecodeOnlineStream(asr_, asr_stream_);
      }
      if (SherpaOnnxOnlineStreamIsEndpoint(asr_, asr_stream_)) {
        const SherpaOnnxOnlineRecognizerResult *r =
            SherpaOnnxGetOnlineStreamResult(asr_, asr_stream_);
        if (r && r->text && r->text[0] != '\0') {
          RCLCPP_INFO(get_logger(), "ASR result: %s", r->text);
          auto msg = std::make_unique<std_msgs::msg::String>();
          msg->data = r->text;
          asr_text_pub_->publish(std::move(msg));
        }
        if (r) {
          SherpaOnnxDestroyOnlineRecognizerResult(r);
        }
        SherpaOnnxOnlineStreamReset(asr_, asr_stream_);
        if (kws_enabled_) {
          mode_ = Mode::KWS;
        }
      }
    }
  }
}

CallbackReturn
AudioPipelineNode::on_deactivate(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "AudioPipelineNode: deactivating");
  running_ = false;
  if (capture_thread_.joinable()) {
    capture_thread_.join();
  }
  if (pcm_) {
    snd_pcm_close(pcm_);
    pcm_ = nullptr;
  }
  return CallbackReturn::SUCCESS;
}

CallbackReturn AudioPipelineNode::on_cleanup(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "AudioPipelineNode: cleaning up");
  if (kws_stream_) {
    SherpaOnnxDestroyOnlineStream(kws_stream_);
    kws_stream_ = nullptr;
  }
  if (kws_) {
    SherpaOnnxDestroyKeywordSpotter(kws_);
    kws_ = nullptr;
  }
  if (asr_stream_) {
    SherpaOnnxDestroyOnlineStream(asr_stream_);
    asr_stream_ = nullptr;
  }
  if (asr_) {
    SherpaOnnxDestroyOnlineRecognizer(asr_);
    asr_ = nullptr;
  }
  wake_word_pub_.reset();
  asr_text_pub_.reset();
  tts_done_pub_.reset();
  sentence_sub_.reset();
  return CallbackReturn::SUCCESS;
}

CallbackReturn AudioPipelineNode::on_shutdown(const rclcpp_lifecycle::State &) {
  RCLCPP_INFO(get_logger(), "AudioPipelineNode: shutting down");
  return CallbackReturn::SUCCESS;
}

CallbackReturn AudioPipelineNode::on_error(const rclcpp_lifecycle::State &) {
  RCLCPP_ERROR(get_logger(), "AudioPipelineNode: error");
  return CallbackReturn::SUCCESS;
}

void AudioPipelineNode::on_sentence(
    const buddy_interfaces::msg::Sentence &msg) {
  RCLCPP_INFO(get_logger(), "TTS: playing sentence [%u]: %s", msg.index,
              msg.text.c_str());
  // TODO: real TTS synthesis + playback should happen here, before signaling
  // done. Currently this is a stub that immediately publishes tts_done so the
  // brain state machine can advance.
  std_msgs::msg::Empty done;
  tts_done_pub_->publish(done);
}

#include <rclcpp_components/register_node_macro.hpp>
RCLCPP_COMPONENTS_REGISTER_NODE(AudioPipelineNode)
