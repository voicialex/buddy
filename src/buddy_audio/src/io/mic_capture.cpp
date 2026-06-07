#include "buddy_audio/io/mic_capture.hpp"

#include <chrono>

AudioCapture::AudioCapture(rclcpp::Logger logger) : logger_(logger) {}

AudioCapture::~AudioCapture() {
    close();
}

bool AudioCapture::configure(const std::string& device, int sample_rate) {
    device_ = device;
    sample_rate_ = sample_rate;
    chunk_size_ = sample_rate / 10;  // 100ms
    raw_buf_.resize(static_cast<size_t>(chunk_size_));
    float_buf_.resize(static_cast<size_t>(chunk_size_));
    return true;
}

bool AudioCapture::open_device() {
    if (pcm_) {
        return true;
    }

    snd_pcm_t* capture = nullptr;
    int rc = snd_pcm_open(&capture, device_.c_str(), SND_PCM_STREAM_CAPTURE, 0);
    if (rc < 0) {
        RCLCPP_WARN(logger_, "[mic] open failed: %s (device=%s). Retrying...", snd_strerror(rc), device_.c_str());
        return false;
    }

    snd_pcm_hw_params_t* hw;
    snd_pcm_hw_params_alloca(&hw);
    snd_pcm_hw_params_any(capture, hw);
    snd_pcm_hw_params_set_access(capture, hw, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(capture, hw, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_channels(capture, hw, 1);
    unsigned int rate = static_cast<unsigned int>(sample_rate_);
    snd_pcm_hw_params_set_rate_near(capture, hw, &rate, nullptr);
    snd_pcm_uframes_t period = static_cast<snd_pcm_uframes_t>(chunk_size_);
    snd_pcm_hw_params_set_period_size_near(capture, hw, &period, nullptr);
    rc = snd_pcm_hw_params(capture, hw);
    if (rc < 0) {
        RCLCPP_WARN(logger_, "[mic] hw_params failed: %s. Retrying...", snd_strerror(rc));
        snd_pcm_close(capture);
        return false;
    }

    pcm_ = capture;
    RCLCPP_INFO(logger_, "[mic] device ready: %s", device_.c_str());
    return true;
}

std::vector<float> AudioCapture::read_chunk() {
    if (!open_device()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        return {};
    }

    snd_pcm_sframes_t n = snd_pcm_readi(pcm_, raw_buf_.data(), static_cast<snd_pcm_uframes_t>(chunk_size_));
    if (n < 0) {
        n = snd_pcm_recover(pcm_, static_cast<int>(n), 0);
        if (n < 0) {
            RCLCPP_WARN(logger_, "[mic] read error: %s. Reconnecting...", snd_strerror(static_cast<int>(n)));
            snd_pcm_close(pcm_);
            pcm_ = nullptr;
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        return {};
    }

    auto count = static_cast<size_t>(n);
    float_buf_.resize(count);
    for (size_t i = 0; i < count; ++i) {
        float_buf_[i] = raw_buf_[i] / 32768.0f;
    }
    return {float_buf_.begin(), float_buf_.begin() + static_cast<ptrdiff_t>(count)};
}

void AudioCapture::close() {
    if (pcm_) {
        snd_pcm_close(pcm_);
        pcm_ = nullptr;
    }
}
