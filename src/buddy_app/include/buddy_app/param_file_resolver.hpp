#pragma once

#include <filesystem>
#include <string>
#include <vector>

// When params_dir is non-empty, uses it directly. Otherwise falls back to
// install_dir/buddy_app/share/buddy_app/params/
std::vector<std::string> resolve_param_files(
    const std::filesystem::path& install_dir,
    const std::string& param_name,
    const std::filesystem::path& params_dir = {});

