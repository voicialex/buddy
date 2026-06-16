#include "buddy_audio/speech_recognizer.hpp"

SpeechRecognizer::SpeechRecognizer(rclcpp::Logger logger) : logger_(logger) {}

SpeechRecognizer::~SpeechRecognizer() {
    cleanup();
}

void SpeechRecognizer::setup_transducer(SherpaOnnxOnlineTransducerModelConfig& trans,
                                        SherpaOnnxOnlineModelConfig& model,
                                        SherpaOnnxFeatureConfig& feat,
                                        int sample_rate,
                                        const std::string& encoder,
                                        const std::string& decoder,
                                        const std::string& joiner,
                                        const std::string& tokens) {
    feat.sample_rate = sample_rate;
    feat.feature_dim = 80;
    trans.encoder = encoder.c_str();
    trans.decoder = decoder.c_str();
    trans.joiner = joiner.c_str();
    model.tokens = tokens.c_str();
    model.provider = "cpu";
    model.num_threads = 1;
}

bool SpeechRecognizer::configure(int sample_rate,
                                 bool kws_enabled,
                                 const KwsConfig& kws_cfg,
                                 std::unique_ptr<AsrBackend> asr_backend) {
    sample_rate_ = sample_rate;
    kws_enabled_ = kws_enabled;

    // KWS init (unchanged)
    if (kws_enabled) {
        if (kws_cfg.encoder.empty()) {
            RCLCPP_ERROR(logger_, "KWS model encoder path not set");
            return false;
        }

        SherpaOnnxKeywordSpotterConfig cfg{};
        setup_transducer(cfg.model_config.transducer,
                         cfg.model_config,
                         cfg.feat_config,
                         sample_rate_,
                         kws_cfg.encoder,
                         kws_cfg.decoder,
                         kws_cfg.joiner,
                         kws_cfg.tokens);
        cfg.keywords_file = kws_cfg.keywords_file.c_str();
        cfg.keywords_threshold = kws_cfg.threshold;
        cfg.keywords_score = kws_cfg.score;
        cfg.max_active_paths = 4;
        RCLCPP_INFO(logger_,
                    "KWS config: keywords_file=%s threshold=%.3f score=%.3f",
                    kws_cfg.keywords_file.c_str(),
                    kws_cfg.threshold,
                    kws_cfg.score);

        kws_ = sherpa_cast(SherpaOnnxCreateKeywordSpotter(&cfg));
        if (!kws_) {
            RCLCPP_ERROR(logger_, "Failed to create KWS");
            return false;
        }
        kws_stream_ = sherpa_cast(SherpaOnnxCreateKeywordStream(kws_));
    }

    // ASR backend (injected)
    asr_backend_ = std::move(asr_backend);
    if (!asr_backend_) {
        RCLCPP_ERROR(logger_, "ASR backend is null");
        return false;
    }

    return true;
}

RecognitionEvent SpeechRecognizer::feed(const float* samples, int n) {
    RecognitionEvent event;

    // KWS always runs when enabled
    if (kws_enabled_ && kws_stream_) {
        SherpaOnnxOnlineStreamAcceptWaveform(kws_stream_, sample_rate_, samples, n);
        while (SherpaOnnxIsKeywordStreamReady(kws_, kws_stream_)) {
            SherpaOnnxDecodeKeywordStream(kws_, kws_stream_);
        }
        const SherpaOnnxKeywordResult* r = SherpaOnnxGetKeywordResult(kws_, kws_stream_);
        if (r && r->keyword && r->keyword[0] != '\0') {
            event.type = RecognitionEvent::WAKE_WORD;
            event.text = r->keyword;
            RCLCPP_INFO(logger_, "[ASR_DIAG] wake_word=%s", event.text.c_str());
            SherpaOnnxDestroyKeywordResult(r);
            SherpaOnnxDestroyOnlineStream(kws_stream_);
            kws_stream_ = sherpa_cast(SherpaOnnxCreateKeywordStream(kws_));
            reset_asr();
            return event;
        }
        if (r) SherpaOnnxDestroyKeywordResult(r);
    }

    // ASR delegated to backend
    if (asr_backend_) {
        AsrResult asr_result = asr_backend_->feed(samples, n);
        if (asr_result.ok()) {
            event.type = RecognitionEvent::ASR_TEXT;
            event.text = asr_result.text;
            RCLCPP_INFO(logger_, "[ASR_DIAG] asr_text=%s", event.text.c_str());
        }
    }

    return event;
}

void SpeechRecognizer::reset_asr() {
    if (asr_backend_) asr_backend_->reset();
}

void SpeechRecognizer::pause_asr(bool paused) {
    if (asr_backend_) asr_backend_->pause(paused);
}

void SpeechRecognizer::cleanup() {
    if (kws_stream_) {
        SherpaOnnxDestroyOnlineStream(kws_stream_);
        kws_stream_ = nullptr;
    }
    if (kws_) {
        SherpaOnnxDestroyKeywordSpotter(kws_);
        kws_ = nullptr;
    }
    asr_backend_.reset();
}
