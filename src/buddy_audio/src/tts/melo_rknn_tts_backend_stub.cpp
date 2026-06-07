#include <rclcpp/rclcpp.hpp>

#include "buddy_audio/tts/tts_backend.hpp"

namespace {
class MeloRknnTtsBackendStub final : public TtsBackend {
public:
    bool initialize(const TtsBackendConfig&, rclcpp::Logger logger) override {
        RCLCPP_ERROR(logger, "Melo RKNN TTS backend is unavailable in this build");
        return false;
    }

    TtsResult generate(const std::string&) override {
        return {};
    }
};
}  // namespace

std::unique_ptr<TtsBackend> create_melo_rknn_tts_backend() {
    return std::make_unique<MeloRknnTtsBackendStub>();
}
