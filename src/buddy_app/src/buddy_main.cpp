#include <yaml-cpp/yaml.h>

#include <class_loader/class_loader.hpp>
#include <exception>
#include <filesystem>
#include <fstream>
#include <lifecycle_msgs/msg/state.hpp>
#include <memory>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp_components/node_factory.hpp>
#include <rclcpp_lifecycle/lifecycle_node.hpp>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "buddy_app/param_file_resolver.hpp"

namespace fs = std::filesystem;

struct ComponentEntry {
    std::string library;
    std::string factory_cls;
    std::string param_name;
    std::string node_name;  // empty = use component's default name
    std::string toggle;     // modules.yaml key (empty = use param_name)
    bool is_lifecycle;
    std::vector<std::pair<std::string, std::string>> remappings;  // topic remaps
};

static const std::vector<ComponentEntry> kComponents = {
    {"libaudio_component.so", "rclcpp_components::NodeFactoryTemplate<AudioPipelineNode>", "audio", "", "", true, {}},
    {"libv4l2_camera.so",
     "rclcpp_components::NodeFactoryTemplate<v4l2_camera::V4L2Camera>",
     "vision",
     "camera_emotion",
     "camera_emotion",
     false,
     {{"image_raw", "camera_emotion/image_raw"}, {"camera_info", "camera_emotion/camera_info"}}},
    {"libv4l2_camera.so",
     "rclcpp_components::NodeFactoryTemplate<v4l2_camera::V4L2Camera>",
     "vision",
     "camera_game",
     "camera_game",
     false,
     {{"image_raw", "camera_game/image_raw"}, {"camera_info", "camera_game/camera_info"}}},
    {"libvision_component.so",
     "rclcpp_components::NodeFactoryTemplate<VisionPipelineNode>",
     "vision",
     "",
     "",
     true,
     {}},
    {"libllm_bridge_component.so",
     "rclcpp_components::NodeFactoryTemplate<LlmBridgeNode>",
     "llm_bridge",
     "",
     "",
     true,
     {}},
    {"libbrain_component.so", "rclcpp_components::NodeFactoryTemplate<BrainNode>", "brain", "", "", true, {}},
};

// Resolve library under <install>/lib/
static std::string find_library(const fs::path& install_dir, const std::string& lib_name, const fs::path& lib_dir = {}) {
    if (!lib_dir.empty()) {
        auto p = lib_dir / lib_name;
        if (fs::exists(p)) return p.string();
    }
    auto p = install_dir / "lib" / lib_name;
    if (fs::exists(p)) return p.string();
    return lib_name;
}

// Load modules.yaml and return the set of disabled module names
static std::set<std::string> load_disabled_modules(const fs::path& params_dir, rclcpp::Logger& logger) {
    auto path = params_dir / "modules.yaml";
    std::set<std::string> disabled;
    if (!fs::exists(path)) {
        RCLCPP_INFO(logger, "No modules.yaml found, all modules enabled");
        return disabled;
    }
    try {
        auto root = YAML::LoadFile(path.string());
        auto modules = root["modules"];
        if (!modules || !modules.IsMap()) return disabled;
        for (auto it = modules.begin(); it != modules.end(); ++it) {
            if (!it->second.as<bool>(true)) {
                disabled.insert(it->first.as<std::string>());
            }
        }
    } catch (const std::exception& e) {
        RCLCPP_WARN(logger, "Failed to parse modules.yaml: %s", e.what());
    }
    return disabled;
}

int main(int argc, char** argv) {
    rclcpp::init(argc, argv);

    // Parse --base-dir for standalone (deb) deployment
    fs::path base_dir;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--base-dir" && i + 1 < argc) {
            base_dir = fs::path(argv[++i]);
            break;
        }
    }

    // Determine install dir from AMENT_PREFIX_PATH (set by setup.bash).
    // Fallback: walk up from /proc/self/exe symlink target.
    fs::path install_dir;
    const char* ament_prefix = std::getenv("AMENT_PREFIX_PATH");
    if (ament_prefix) {
        // AMENT_PREFIX_PATH is colon-separated; find the buddy_app entry
        std::string path_list(ament_prefix);
        std::istringstream ss(path_list);
        std::string token;
        while (std::getline(ss, token, ':')) {
            if (token.find("buddy_app") != std::string::npos) {
                // token = <install>/buddy_app → parent is install dir
                install_dir = fs::path(token).parent_path();
                break;
            }
        }
    }
    if (install_dir.empty()) {
        auto exe = fs::read_symlink("/proc/self/exe");
        install_dir = exe.parent_path().parent_path().parent_path().parent_path();
    }

    // Deb mode: --base-dir overrides path resolution
    fs::path params_dir;
    fs::path lib_dir;
    if (!base_dir.empty()) {
        params_dir = base_dir / "params";
        lib_dir = base_dir / "lib";
        install_dir = base_dir;
    } else {
        params_dir = install_dir / "buddy_app" / "share" / "buddy_app" / "params";
    }

    rclcpp::executors::MultiThreadedExecutor executor;

    std::vector<std::unique_ptr<class_loader::ClassLoader>> loaders;
    std::vector<rclcpp_components::NodeInstanceWrapper> wrappers;
    std::vector<std::shared_ptr<rclcpp_lifecycle::LifecycleNode>> lifecycle_nodes;

    auto logger = rclcpp::get_logger("buddy_main");

    auto disabled = load_disabled_modules(params_dir, logger);
    if (!disabled.empty()) {
        for (auto& name : disabled) {
            RCLCPP_DEBUG(logger, "Module [%s] disabled via modules.yaml", name.c_str());
        }
    }

    RCLCPP_DEBUG(logger, "Loading %zu components...", kComponents.size());

    for (const auto& entry : kComponents) {
        const auto& toggle_key = entry.toggle.empty() ? entry.param_name : entry.toggle;
        if (disabled.count(toggle_key)) {
            RCLCPP_INFO(logger, "Skipping disabled module: %s (%s)", toggle_key.c_str(), entry.library.c_str());
            continue;
        }
        auto lib_path = find_library(install_dir, entry.library, lib_dir);
        RCLCPP_DEBUG(logger, "Loading %s", lib_path.c_str());

        auto loader = std::make_unique<class_loader::ClassLoader>(lib_path);

        // Build NodeOptions with intra-process and params
        rclcpp::NodeOptions opts;
        opts.use_intra_process_comms(true);
        const std::vector<std::string> param_files = resolve_param_files(install_dir, entry.param_name, params_dir);

        // Build argument list
        std::vector<std::string> args = {"--ros-args"};
        for (const auto& param_file : param_files) {
            args.push_back("--params-file");
            args.push_back(param_file);
            RCLCPP_DEBUG(logger, "  params: %s", param_file.c_str());
        }
        if (!entry.node_name.empty()) {
            args.push_back("-r");
            args.push_back("__node:=" + entry.node_name);
        }
        for (auto& [from, to] : entry.remappings) {
            args.push_back("-r");
            args.push_back(from + ":=" + to);
        }
        if (args.size() > 1) {
            opts.arguments(args);
        }

        // Create NodeFactory → NodeInstanceWrapper
        auto factory = loader->createInstance<rclcpp_components::NodeFactory>(entry.factory_cls);
        loaders.push_back(std::move(loader));  // keep loader alive as long as the node
        auto wrapper = factory->create_node_instance(opts);
        executor.add_node(wrapper.get_node_base_interface());

        auto node_instance = wrapper.get_node_instance();

        if (entry.is_lifecycle) {
            auto lc_node = std::static_pointer_cast<rclcpp_lifecycle::LifecycleNode>(node_instance);
            lifecycle_nodes.push_back(lc_node);
        } else {
            RCLCPP_DEBUG(logger, "  %s: non-lifecycle node, added to executor only", entry.param_name.c_str());
        }

        wrappers.push_back(std::move(wrapper));
    }

    // Lifecycle: configure → activate
    using CBReturn = rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn;
    RCLCPP_DEBUG(logger, "Configuring %zu lifecycle nodes...", lifecycle_nodes.size());
    for (auto& node : lifecycle_nodes) {
        CBReturn cb_rc = CBReturn::SUCCESS;
        node->configure(cb_rc);
        if (cb_rc == CBReturn::SUCCESS) {
            RCLCPP_DEBUG(logger, "  %s: configured", node->get_name());
        } else {
            RCLCPP_ERROR(logger, "  %s: configure FAILED (%d)", node->get_name(), static_cast<int>(cb_rc));
            rclcpp::shutdown();
            return 1;
        }
    }
    RCLCPP_DEBUG(logger, "Activating...");
    for (auto& node : lifecycle_nodes) {
        CBReturn cb_rc = CBReturn::SUCCESS;
        node->activate(cb_rc);
        if (cb_rc == CBReturn::SUCCESS) {
            RCLCPP_DEBUG(logger, "  %s: activated", node->get_name());
        } else {
            RCLCPP_ERROR(logger, "  %s: activate FAILED (%d)", node->get_name(), static_cast<int>(cb_rc));
            rclcpp::shutdown();
            return 1;
        }
    }

    RCLCPP_DEBUG(logger, "All nodes active. Spinning...");

    // Read KWS state from resolved audio param files (split or legacy) and
    // display appropriate banner.
    bool kws_enabled = true;
    try {
        const auto audio_param_files = resolve_param_files(install_dir, "audio", params_dir);
        for (const auto& param_file : audio_param_files) {
            auto cfg = YAML::LoadFile(param_file);
            auto node = cfg["audio"]["ros__parameters"]["kws"]["enable"];
            if (node) {
                kws_enabled = node.as<bool>(kws_enabled);
            }
        }
    } catch (const std::exception& e) {
        RCLCPP_WARN(logger, "Failed to read audio params for KWS banner: %s (banner may be incorrect)", e.what());
    }

    if (kws_enabled) {
        // Read wake word from params/keywords.txt (format: "tokens @DisplayName")
        std::string wake_word = "wake word";
        auto kw_path = params_dir / "keywords.txt";
        std::ifstream kw_file(kw_path);
        if (kw_file.is_open()) {
            std::string line;
            if (std::getline(kw_file, line)) {
                auto at = line.find('@');
                if (at != std::string::npos) {
                    wake_word = line.substr(at + 1);
                }
            }
        }
        RCLCPP_INFO(logger,
                    "\n"
                    "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n"
                    "  Buddy is READY\n"
                    "  Say \"%s\" to start\n"
                    "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━",
                    wake_word.c_str());
    } else {
        RCLCPP_INFO(logger,
                    "\n"
                    "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n"
                    "  Buddy is READY\n"
                    "  KWS disabled — listening immediately\n"
                    "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    }
    executor.spin();

    if (rclcpp::ok()) {
        // Graceful shutdown while context is valid.
        for (auto& node : lifecycle_nodes) {
            try {
                auto state_id = node->get_current_state().id();
                using S = lifecycle_msgs::msg::State;
                if (state_id == S::PRIMARY_STATE_ACTIVE) {
                    node->deactivate();
                    state_id = node->get_current_state().id();
                }
                if (state_id == S::PRIMARY_STATE_INACTIVE) {
                    node->cleanup();
                    state_id = node->get_current_state().id();
                }
                if (state_id == S::PRIMARY_STATE_UNCONFIGURED || state_id == S::PRIMARY_STATE_INACTIVE) {
                    node->shutdown();
                }
            } catch (const std::exception& e) {
                RCLCPP_WARN(logger, "  %s: shutdown transition skipped: %s", node->get_name(), e.what());
            } catch (...) {
                RCLCPP_WARN(logger, "  %s: shutdown transition skipped: unknown error", node->get_name());
            }
        }
    } else {
        RCLCPP_INFO(logger, "ROS context already shutdown, skip manual lifecycle transitions");
    }

    rclcpp::shutdown();
    return 0;
}
