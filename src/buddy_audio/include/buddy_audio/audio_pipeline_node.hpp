#pragma once
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

#include <alsa/asoundlib.h>
#include <sherpa-onnx/c-api/c-api.h>

#include <buddy_interfaces/msg/sentence.hpp>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp_lifecycle/lifecycle_node.hpp>
#include <std_msgs/msg/empty.hpp>
#include <std_msgs/msg/string.hpp>

using CallbackReturn =
    rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn;

class AudioPipelineNode : public rclcpp_lifecycle::LifecycleNode {
public:
  explicit AudioPipelineNode(
      const rclcpp::NodeOptions &options = rclcpp::NodeOptions());
  CallbackReturn on_configure(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_activate(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_deactivate(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_cleanup(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_shutdown(const rclcpp_lifecycle::State &) override;
  CallbackReturn on_error(const rclcpp_lifecycle::State &) override;

private:
  enum class Mode { KWS, ASR };

  void capture_loop();
  void on_sentence(const buddy_interfaces::msg::Sentence &msg);
  void tts_loop();
  void play_speech(const float *samples, int32_t n, int32_t sample_rate);

  static void setup_transducer_config(
      SherpaOnnxOnlineTransducerModelConfig &transducer,
      SherpaOnnxOnlineModelConfig &model_cfg, SherpaOnnxFeatureConfig &feat_cfg,
      int sample_rate, const std::string &encoder, const std::string &decoder,
      const std::string &joiner, const std::string &tokens);

  // Sherpa-ONNX handles (non-const: AcceptWaveform/Decode/etc modify internal state)
  SherpaOnnxKeywordSpotter *kws_ = nullptr;
  SherpaOnnxOnlineStream *kws_stream_ = nullptr;
  SherpaOnnxOnlineRecognizer *asr_ = nullptr;
  SherpaOnnxOnlineStream *asr_stream_ = nullptr;
  SherpaOnnxOfflineTts *tts_ = nullptr;

  // ALSA capture
  snd_pcm_t *pcm_ = nullptr;

  // TTS worker thread
  std::thread tts_thread_;
  std::mutex tts_queue_mtx_;
  std::condition_variable tts_queue_cv_;
  std::queue<buddy_interfaces::msg::Sentence> tts_queue_;

  // Capture thread
  std::thread capture_thread_;
  std::atomic<bool> running_{false};
  std::atomic<Mode> mode_{Mode::KWS};

  // ROS interfaces
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr wake_word_pub_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr asr_text_pub_;
  rclcpp::Publisher<std_msgs::msg::Empty>::SharedPtr tts_done_pub_;
  rclcpp::Subscription<buddy_interfaces::msg::Sentence>::SharedPtr
      sentence_sub_;

  // Config
  int sample_rate_ = 16000;
  int tts_sid_ = 0;
  float tts_speed_ = 1.0f;
  std::string mic_device_ = "default";
  std::string speaker_device_ = "default";
  bool kws_enabled_ = true;
  std::string current_turn_id_;
};
