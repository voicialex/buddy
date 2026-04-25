#include <rclcpp/rclcpp.hpp>
#include <rclcpp/executors/multi_threaded_executor.hpp>
#include <class_loader/class_loader.hpp>
#include <rclcpp_components/node_factory.hpp>
#include <ament_index_cpp/get_package_prefix.hpp>

int main(int argc, char **argv) {
  rclcpp::init(argc, argv);

  auto exec = std::make_shared<rclcpp::executors::MultiThreadedExecutor>();

  const struct {
    const char *package;
    const char *plugin;
  } components[] = {
    {"buddy_audio", "rclcpp_components::NodeFactoryTemplate<AudioPipelineNode>"},
    {"buddy_vision", "rclcpp_components::NodeFactoryTemplate<VisionPipelineNode>"},
    {"buddy_cloud", "rclcpp_components::NodeFactoryTemplate<CloudClientNode>"},
    {"buddy_dialog", "rclcpp_components::NodeFactoryTemplate<DialogManagerNode>"},
    {"buddy_sentence", "rclcpp_components::NodeFactoryTemplate<SentenceSegmenterNode>"},
    {"buddy_state_machine", "rclcpp_components::NodeFactoryTemplate<StateMachineNode>"},
  };

  std::vector<std::shared_ptr<class_loader::ClassLoader>> loaders;
  for (const auto &c : components) {
    auto lib_path = ament_index_cpp::get_package_prefix(c.package) + "/lib/lib" + c.package + "_component.so";
    auto loader = std::make_shared<class_loader::ClassLoader>(lib_path);
    auto factory = loader->createInstance<rclcpp_components::NodeFactory>(c.plugin);
    auto wrapper = factory->create_node_instance(rclcpp::NodeOptions());
    exec->add_node(wrapper.get_node_base_interface());
    RCLCPP_INFO(rclcpp::get_logger("buddy_app"), "Loaded: %s", c.package);
    loaders.push_back(loader);
  }

  RCLCPP_INFO(rclcpp::get_logger("buddy_app"), "All %zu components loaded", loaders.size());
  exec->spin();
  rclcpp::shutdown();
  return 0;
}
