#include <class_loader/class_loader.hpp>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp_components/node_factory.hpp>
#include <rclcpp_lifecycle/lifecycle_node.hpp>
#include <lifecycle_msgs/msg/state.hpp>

#include <filesystem>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include <yaml-cpp/yaml.h>

namespace fs = std::filesystem;

struct ComponentEntry {
  std::string library;     // e.g. "libaudio_component.so"
  std::string factory_cls; // registered NodeFactory class name
  std::string param_name;  // param yaml file stem
};

static const std::vector<ComponentEntry> kComponents = {
    {"libaudio_component.so",
     "rclcpp_components::NodeFactoryTemplate<AudioPipelineNode>", "audio"},
    {"libvision_component.so",
     "rclcpp_components::NodeFactoryTemplate<VisionPipelineNode>", "vision"},
    {"libcloud_component.so",
     "rclcpp_components::NodeFactoryTemplate<CloudClientNode>", "cloud"},
    {"libstate_machine_component.so",
     "rclcpp_components::NodeFactoryTemplate<StateMachineNode>",
     "state_machine"},
    {"libdialog_component.so",
     "rclcpp_components::NodeFactoryTemplate<DialogManagerNode>", "dialog"},
    {"libsentence_component.so",
     "rclcpp_components::NodeFactoryTemplate<SentenceSegmenterNode>",
     "sentence"},
};

// Resolve path to <install>/buddy_app/share/buddy_app/params/<name>.yaml
static std::string find_param_file(const fs::path &install_dir,
                                   const std::string &name) {
  return (install_dir / "buddy_app" / "share" / "buddy_app" / "params" /
          (name + ".yaml"))
      .string();
}

// Resolve library under <install>/lib/
static std::string find_library(const fs::path &install_dir,
                                const std::string &lib_name) {
  auto p = install_dir / "lib" / lib_name;
  if (fs::exists(p))
    return p.string();
  return lib_name; // fallback to LD_LIBRARY_PATH
}

// Load modules.yaml and return the set of disabled module names
static std::set<std::string> load_disabled_modules(const fs::path &install_dir,
                                                    rclcpp::Logger &logger) {
  std::set<std::string> disabled;
  auto path = install_dir / "buddy_app" / "share" / "buddy_app" / "params" /
              "modules.yaml";
  if (!fs::exists(path)) {
    RCLCPP_INFO(logger, "No modules.yaml found, all modules enabled");
    return disabled;
  }
  try {
    auto root = YAML::LoadFile(path.string());
    auto modules = root["modules"];
    if (!modules || !modules.IsMap())
      return disabled;
    for (auto it = modules.begin(); it != modules.end(); ++it) {
      if (!it->second.as<bool>(true)) {
        disabled.insert(it->first.as<std::string>());
      }
    }
  } catch (const std::exception &e) {
    RCLCPP_WARN(logger, "Failed to parse modules.yaml: %s", e.what());
  }
  return disabled;
}

int main(int argc, char **argv) {
  rclcpp::init(argc, argv);

  // Determine install dir from AMENT_PREFIX_PATH (set by setup.bash).
  // Fallback: walk up from /proc/self/exe symlink target.
  fs::path install_dir;
  const char *ament_prefix = std::getenv("AMENT_PREFIX_PATH");
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

  rclcpp::executors::MultiThreadedExecutor executor;

  std::vector<std::unique_ptr<class_loader::ClassLoader>> loaders;
  std::vector<rclcpp_components::NodeInstanceWrapper> wrappers;
  std::vector<std::shared_ptr<rclcpp_lifecycle::LifecycleNode>> lifecycle_nodes;

  auto logger = rclcpp::get_logger("buddy_main");

  auto disabled = load_disabled_modules(install_dir, logger);
  if (!disabled.empty()) {
    for (auto &name : disabled) {
      RCLCPP_DEBUG(logger, "Module [%s] disabled via modules.yaml", name.c_str());
    }
  }

  RCLCPP_INFO(logger, "Loading %zu components...", kComponents.size());

  for (const auto &entry : kComponents) {
    if (disabled.count(entry.param_name)) {
      RCLCPP_DEBUG(logger, "Skipping disabled module: %s",
                  entry.param_name.c_str());
      continue;
    }
    auto lib_path = find_library(install_dir, entry.library);
    RCLCPP_INFO(logger, "Loading %s", lib_path.c_str());

    auto loader = std::make_unique<class_loader::ClassLoader>(lib_path);

    // Build NodeOptions with intra-process and params
    rclcpp::NodeOptions opts;
    opts.use_intra_process_comms(true);
    auto param_file = find_param_file(install_dir, entry.param_name);
    if (fs::exists(param_file)) {
      opts.arguments({"--ros-args", "--params-file", param_file});
      RCLCPP_INFO(logger, "  params: %s", param_file.c_str());
    }

    // Create NodeFactory → NodeInstanceWrapper
    auto factory = loader->createInstance<rclcpp_components::NodeFactory>(
        entry.factory_cls);
    auto wrapper = factory->create_node_instance(opts);
    executor.add_node(wrapper.get_node_base_interface());

    // We know all our nodes are LifecycleNodes
    auto lc_node = std::static_pointer_cast<rclcpp_lifecycle::LifecycleNode>(
        wrapper.get_node_instance());
    lifecycle_nodes.push_back(lc_node);

    wrappers.push_back(std::move(wrapper));
    loaders.push_back(std::move(loader));
  }

  // Lifecycle: configure → activate
  RCLCPP_INFO(logger, "Configuring %zu lifecycle nodes...",
              lifecycle_nodes.size());
  for (auto &node : lifecycle_nodes) {
    node->configure();
  }
  RCLCPP_INFO(logger, "Activating...");
  for (auto &node : lifecycle_nodes) {
    node->activate();
  }

  RCLCPP_INFO(logger, "All nodes active. Spinning...");
  executor.spin();

  // Graceful shutdown — nodes may already be transitioning from signal handler
  for (auto &node : lifecycle_nodes) {
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
      if (state_id == S::PRIMARY_STATE_UNCONFIGURED ||
          state_id == S::PRIMARY_STATE_INACTIVE) {
        node->shutdown();
      }
    } catch (...) {
      // Node already shut down or context invalidated — ignore
    }
  }

  rclcpp::shutdown();
  return 0;
}
