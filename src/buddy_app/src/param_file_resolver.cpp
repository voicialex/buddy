#include "buddy_app/param_file_resolver.hpp"

namespace fs = std::filesystem;

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

    const fs::path base_path = dir / (param_name + ".yaml");
    if (fs::exists(base_path)) {
        files.push_back(base_path.string());
    }

    if (param_name == "cloud") {
        const fs::path secret_path = dir / "cloud.secret.yaml";
        if (fs::exists(secret_path)) {
            files.push_back(secret_path.string());
        }
    }

    return files;
}
