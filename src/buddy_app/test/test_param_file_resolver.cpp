#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "buddy_app/param_file_resolver.hpp"

namespace fs = std::filesystem;

class ParamFileResolverTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto unique_name =
            "buddy_param_resolver_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        install_dir_ = fs::temp_directory_path() / unique_name;
        fs::create_directories(params_dir());
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(install_dir_, ec);
    }

    fs::path params_dir() const { return install_dir_ / "buddy_app" / "share" / "buddy_app" / "params"; }

    fs::path install_dir_;
};

TEST_F(ParamFileResolverTest, CloudReturnsBaseAndSecretWhenBothExist) {
    const fs::path base_path = params_dir() / "cloud.yaml";
    const fs::path secret_path = params_dir() / "cloud.secret.yaml";
    std::ofstream(base_path.string()) << "cloud:\n";
    std::ofstream(secret_path.string()) << "secret:\n";

    const auto files = resolve_param_files(install_dir_, "cloud");
    ASSERT_EQ(files.size(), 2u);
    EXPECT_EQ(files[0], base_path.string());
    EXPECT_EQ(files[1], secret_path.string());
}

TEST_F(ParamFileResolverTest, CloudReturnsOnlyBaseWhenSecretMissing) {
    const fs::path base_path = params_dir() / "cloud.yaml";
    std::ofstream(base_path.string()) << "cloud:\n";

    const auto files = resolve_param_files(install_dir_, "cloud");
    ASSERT_EQ(files.size(), 1u);
    EXPECT_EQ(files[0], base_path.string());
}

TEST_F(ParamFileResolverTest, ReturnsEmptyWhenBaseMissing) {
    const auto files = resolve_param_files(install_dir_, "cloud");
    EXPECT_TRUE(files.empty());
}

TEST_F(ParamFileResolverTest, AudioUsesSplitFilesInStableOrder) {
    const fs::path device_path = params_dir() / "audio.device.yaml";
    const fs::path asr_path = params_dir() / "audio.asr.yaml";
    const fs::path tts_path = params_dir() / "audio.tts.yaml";
    const fs::path webrtc_path = params_dir() / "audio.webrtc.yaml";

    std::ofstream(device_path.string()) << "audio:\n";
    std::ofstream(asr_path.string()) << "audio:\n";
    std::ofstream(tts_path.string()) << "audio:\n";
    std::ofstream(webrtc_path.string()) << "audio:\n";
    const auto files = resolve_param_files(install_dir_, "audio");
    ASSERT_EQ(files.size(), 4u);
    EXPECT_EQ(files[0], device_path.string());
    EXPECT_EQ(files[1], asr_path.string());
    EXPECT_EQ(files[2], tts_path.string());
    EXPECT_EQ(files[3], webrtc_path.string());
}

TEST_F(ParamFileResolverTest, AudioDoesNotFallbackToLegacyAudioYaml) {
    const fs::path legacy_path = params_dir() / "audio.yaml";
    std::ofstream(legacy_path.string()) << "audio:\n";

    const auto files = resolve_param_files(install_dir_, "audio");
    EXPECT_TRUE(files.empty());
}

TEST_F(ParamFileResolverTest, VisionUsesDeviceThenVisionYaml) {
    const fs::path device_path = params_dir() / "vision.device.yaml";
    const fs::path vision_path = params_dir() / "vision.yaml";
    std::ofstream(device_path.string()) << "camera_emotion:\n";
    std::ofstream(vision_path.string()) << "vision:\n";

    const auto files = resolve_param_files(install_dir_, "vision");
    ASSERT_EQ(files.size(), 2u);
    EXPECT_EQ(files[0], device_path.string());
    EXPECT_EQ(files[1], vision_path.string());
}

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
