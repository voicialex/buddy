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

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
