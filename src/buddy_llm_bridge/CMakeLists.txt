cmake_minimum_required(VERSION 3.8)
project(buddy_llm_bridge)
if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_lifecycle REQUIRED)
find_package(rclcpp_components REQUIRED)
find_package(rclcpp_action REQUIRED)
find_package(buddy_interfaces REQUIRED)
find_package(CURL REQUIRED)
find_package(OpenCV REQUIRED COMPONENTS imgcodecs)

add_library(llm_bridge_component SHARED src/llm_bridge_node.cpp)
target_include_directories(
  llm_bridge_component PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>
)
target_compile_features(llm_bridge_component PUBLIC cxx_std_17)
target_link_libraries(llm_bridge_component CURL::libcurl ${OpenCV_LIBS})
ament_target_dependencies(
  llm_bridge_component rclcpp rclcpp_lifecycle rclcpp_components rclcpp_action buddy_interfaces
)
rclcpp_components_register_nodes(llm_bridge_component "LlmBridgeNode")
install(
  TARGETS llm_bridge_component
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)
ament_package()
