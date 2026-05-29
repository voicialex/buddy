#pragma once
#include <alsa/asoundlib.h>

#include <atomic>
#include <rclcpp/rclcpp.hpp>
#include <string>
#include <vector>

class AudioCapture {
public:
    explicit AudioCapture(rclcpp::Logger logger);
    ~AudioCapture();

    bool configure(const std::string& device, int sample_rate);
    // Returns chunk of float samples, or empty if device unavailable.
    std::vector<float> read_chunk();
    void close();

private:
    bool open_device();

    rclcpp::Logger logger_;
    std::string device_ = "default";
    int sample_rate_ = 16000;
    int chunk_size_ = 1600;  // 100ms at 16kHz
    snd_pcm_t* pcm_ = nullptr;

    std::vector<int16_t> raw_buf_;
    std::vector<float> float_buf_;
};
