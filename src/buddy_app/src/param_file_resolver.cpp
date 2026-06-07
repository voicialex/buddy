#include "buddy_app/param_file_resolver.hpp"

namespace fs = std::filesystem;

namespace {
void push_if_exists(std::vector<std::string>& files, const fs::path& p) {
    if (fs::exists(p)) {
        files.push_back(p.string());
    }
}
}  // namespace

std::vector<std::string> resolve_param_files(
    const fs::path& install_dir,
    const std::string& param_name,
    const fs::path& params_dir) {
    std::vector<std::string> files;
    if (param_name.empty()) {
        return files;
    }

    const fs::path dir = params_dir.empty()
        ? install_dir / "buddy_app" / "share" / "buddy_app" / "params"
        : params_dir;

    if (param_name == "audio") {
        // Prefer split audio configs for maintainability.
        push_if_exists(files, dir / "audio.device.yaml");
        push_if_exists(files, dir / "audio.asr.yaml");
        push_if_exists(files, dir / "audio.tts.yaml");
        push_if_exists(files, dir / "audio.webrtc.yaml");
        return files;
    }
    if (param_name == "vision") {
        // Load camera device config first, then vision inference config.
        push_if_exists(files, dir / "vision.device.yaml");
        push_if_exists(files, dir / "vision.yaml");
        if (!files.empty()) {
            return files;
        }
    }

    const fs::path base_path = dir / (param_name + ".yaml");
    push_if_exists(files, base_path);

    if (param_name == "cloud") {
        const fs::path secret_path = dir / "cloud.secret.yaml";
        if (fs::exists(secret_path)) {
            files.push_back(secret_path.string());
        }
    }

    return files;
}
