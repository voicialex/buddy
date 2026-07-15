#include "buddy_audio/io/speaker_player.hpp"

#include <algorithm>
#include <unordered_map>

TtsPlayer::TtsPlayer(rclcpp::Logger logger) : logger_(logger) {}

TtsPlayer::~TtsPlayer() {
    stop();
}

std::string TtsPlayer::clean_tts_text(const std::string& text) {
    static const std::unordered_map<std::string, std::string> kFullWidthPunc = {
        {"，", ","}, {"、", ","}, {"：", ","}, {"；", ","}, {"·", ","},
        {"。", "."}, {"！", "!"}, {"？", "?"},
        {"“", "'"}, {"”", "'"}, {"‘", "'"}, {"’", "'"},
        {"（", "'"}, {"）", "'"}, {"《", "'"}, {"》", "'"},
        {"【", "'"}, {"】", "'"}, {"「", "'"}, {"」", "'"},
        {"—", "-"}, {"～", "-"},
    };

    std::string out;
    out.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
        unsigned char c = text[i];

        // ASCII: keep alnum, space, newline, and common punctuation that
        // sherpa melo recognizes (,.!?'-); drop everything else (*#+= etc.)
        if (c < 0x80) {
            const bool alnum = (c >= '0' && c <= '9') ||
                               (c >= 'A' && c <= 'Z') ||
                               (c >= 'a' && c <= 'z');
            const bool allowed_punc = c == ' ' || c == '\n' ||
                                      c == ',' || c == '.' || c == '!' ||
                                      c == '?' || c == '\'' || c == '-';
            if (alnum || allowed_punc) {
                out += static_cast<char>(c);
            }
            ++i;
            continue;
        }

        // Determine UTF-8 char length
        size_t char_len = 0;
        if ((c & 0xE0) == 0xC0) char_len = 2;
        else if ((c & 0xF0) == 0xE0) char_len = 3;
        else if ((c & 0xF8) == 0xF0) char_len = 4;
        else { ++i; continue; }  // invalid leading byte
        if (i + char_len > text.size()) break;  // truncated

        std::string ch = text.substr(i, char_len);

        // 4-byte: emoji/rare symbols, skip
        if (char_len == 4) {
            i += char_len;
            continue;
        }

        // 3-byte: full-width punctuation → half-width
        if (char_len == 3) {
            auto it = kFullWidthPunc.find(ch);
            if (it != kFullWidthPunc.end()) {
                out += it->second;
                i += char_len;
                continue;
            }
        }

        out += ch;
        i += char_len;
    }
    return out;
}

bool TtsPlayer::configure(std::unique_ptr<TtsBackend> backend,
                          const std::string& speaker_device,
                          DoneCallback on_done,
                          RenderTapCallback on_render) {
    backend_ = std::move(backend);
    speaker_device_ = speaker_device;
    on_done_ = std::move(on_done);
    on_render_ = std::move(on_render);
    return backend_ != nullptr;
}

void TtsPlayer::start() {
    running_ = true;
    thread_ = std::thread(&TtsPlayer::tts_loop, this);
}

void TtsPlayer::stop() {
    running_ = false;
    queue_cv_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
    close_playback();
}

void TtsPlayer::enqueue(const buddy_interfaces::msg::Sentence& msg) {
    std::lock_guard<std::mutex> lock(queue_mtx_);
    // Drop oldest if queue full
    while (queue_.size() >= MAX_QUEUE_SIZE) {
        queue_.pop();
    }
    queue_.push(msg);
    queue_cv_.notify_one();
}

void TtsPlayer::clear_queue() {
    std::lock_guard<std::mutex> lock(queue_mtx_);
    std::queue<buddy_interfaces::msg::Sentence> empty;
    queue_.swap(empty);
}

void TtsPlayer::interrupt_now() {
    interrupt_requested_.store(true);
    clear_queue();
    queue_cv_.notify_all();
}

void TtsPlayer::clear_interrupt() {
    interrupt_requested_.store(false);
}

size_t TtsPlayer::pending_queue_size_for_test() {
    std::lock_guard<std::mutex> lock(queue_mtx_);
    return queue_.size();
}

bool TtsPlayer::ensure_playback_open(int32_t sample_rate) {
    if (playback_ && playback_rate_ == sample_rate) {
        return true;
    }
    close_playback();

    for (int attempt = 0; attempt < MAX_OPEN_RETRIES && running_; ++attempt) {
        int rc = snd_pcm_open(&playback_, speaker_device_.c_str(), SND_PCM_STREAM_PLAYBACK, 0);
        if (rc < 0) {
            RCLCPP_WARN(logger_,
                        "[speaker] open failed: %s (device=%s, attempt %d)",
                        snd_strerror(rc),
                        speaker_device_.c_str(),
                        attempt + 1);
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }

        snd_pcm_hw_params_t* hw;
        snd_pcm_hw_params_alloca(&hw);
        snd_pcm_hw_params_any(playback_, hw);
        snd_pcm_hw_params_set_access(playback_, hw, SND_PCM_ACCESS_RW_INTERLEAVED);
        snd_pcm_hw_params_set_format(playback_, hw, SND_PCM_FORMAT_S16_LE);
        snd_pcm_hw_params_set_channels(playback_, hw, 1);
        unsigned int rate = static_cast<unsigned int>(sample_rate);
        snd_pcm_hw_params_set_rate_near(playback_, hw, &rate, nullptr);
        snd_pcm_uframes_t period_size = 1024;
        snd_pcm_hw_params_set_period_size_near(playback_, hw, &period_size, nullptr);
        snd_pcm_uframes_t buffer_size = 16384;
        snd_pcm_hw_params_set_buffer_size_near(playback_, hw, &buffer_size);
        rc = snd_pcm_hw_params(playback_, hw);
        if (rc < 0) {
            RCLCPP_WARN(logger_, "[speaker] hw_params failed: %s", snd_strerror(rc));
            snd_pcm_close(playback_);
            playback_ = nullptr;
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }

        playback_rate_ = sample_rate;
        RCLCPP_INFO(logger_, "[speaker] device ready: %s (period=%lu buffer=%lu)",
                    speaker_device_.c_str(),
                    static_cast<unsigned long>(period_size),
                    static_cast<unsigned long>(buffer_size));
        return true;
    }

    RCLCPP_WARN(logger_, "[speaker] failed to open after %d retries, skipping TTS", MAX_OPEN_RETRIES);
    return false;
}

void TtsPlayer::close_playback() {
    if (playback_) {
        snd_pcm_drain(playback_);
        snd_pcm_close(playback_);
        playback_ = nullptr;
        playback_rate_ = 0;
    }
}

bool TtsPlayer::play_speech(const float* samples, int32_t n, int32_t sample_rate) {
    const auto t_start = std::chrono::steady_clock::now();
    if (!ensure_playback_open(sample_rate)) {
        return false;
    }

    // generate() 期间可能被 interrupt_now() 设置标记，此时跳过播放。
    // 标记的跨 turn 清理由 on_sentence 检测 turn_id 变化时调用 clear_interrupt()。
    if (interrupt_requested_.load()) {
        interrupt_requested_.store(false);
        return false;
    }

    std::vector<int16_t> pcm_buf(static_cast<size_t>(n));
    for (int32_t i = 0; i < n; ++i) {
        float s = std::clamp(samples[i], -1.0f, 1.0f);
        pcm_buf[static_cast<size_t>(i)] = static_cast<int16_t>(s * 32767.0f);
    }

    const int chunk_size = 1024;
    int32_t offset = 0;
    while (offset < n && running_) {
        if (interrupt_requested_.load()) {
            if (playback_) {
                snd_pcm_drop(playback_);
                snd_pcm_prepare(playback_);
            }
            interrupt_requested_.store(false);
            return false;
        }
        int32_t to_write = std::min(n - offset, chunk_size);
        if (on_render_) {
            on_render_(samples + offset, to_write, sample_rate);
        }
        snd_pcm_sframes_t written =
            snd_pcm_writei(playback_, pcm_buf.data() + offset, static_cast<snd_pcm_uframes_t>(to_write));
        if (written < 0) {
            written = snd_pcm_recover(playback_, static_cast<int>(written), 0);
            if (written < 0) {
                RCLCPP_WARN(logger_, "[speaker] write error: %s", snd_strerror(static_cast<int>(written)));
                close_playback();
                if (!ensure_playback_open(sample_rate)) {
                    return false;
                }
                continue;
            }
        }
        offset += static_cast<int32_t>(written);
    }

    last_play_time_ = std::chrono::steady_clock::now();
    const auto play_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        last_play_time_ - t_start).count();
    RCLCPP_INFO(logger_, "play_speech: %d samples @ %dHz (%ldms)", n, sample_rate, play_ms);
    return offset >= n;
}

void TtsPlayer::tts_loop() {
    for (;;) {
        buddy_interfaces::msg::Sentence msg;
        {
            std::unique_lock<std::mutex> lock(queue_mtx_);
            // Wait with timeout so we can close idle playback device
            bool got_msg =
                queue_cv_.wait_for(lock, IDLE_CLOSE_TIMEOUT, [this] { return !queue_.empty() || !running_; });
            if (!got_msg && queue_.empty()) {
                // Idle timeout — close playback to free device
                close_playback();
                if (!running_) {
                    return;
                }
                continue;
            }
            if (queue_.empty()) {
                return;  // running_ == false
            }
            msg = queue_.front();
            queue_.pop();
        }

        bool playback_ok = true;
        if (!msg.text.empty()) {
            auto clean = clean_tts_text(msg.text);
            if (!clean.empty()) {
                const auto t0 = std::chrono::steady_clock::now();
                auto result = backend_->generate(clean);
                const auto gen_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - t0).count();
                RCLCPP_INFO(logger_, "TTS generate: %zu samples (%ldms)", result.samples.size(), gen_ms);
                if (result.ok()) {
                    playback_ok = play_speech(
                        result.samples.data(), static_cast<int32_t>(result.samples.size()), result.sample_rate);
                }
            }
        }

        if (msg.is_final && playback_ok) {
            RCLCPP_INFO(logger_, "TTS done");
            if (on_done_) {
                on_done_();
            }
        } else if (msg.is_final && !playback_ok) {
            RCLCPP_WARN(logger_, "TTS final sentence not fully played, skip tts_done");
        }
    }
}
