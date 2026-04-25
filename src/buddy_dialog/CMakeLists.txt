cmake_minimum_required(VERSION 3.8)
project(buddy_dialog)
if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_lifecycle REQUIRED)
find_package(rclcpp_components REQUIRED)
find_package(buddy_interfaces REQUIRED)

add_library(dialog_component SHARED src/dialog_manager_node.cpp)
target_include_directories(
  dialog_component PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                          $<INSTALL_INTERFACE:include>)
target_compile_features(dialog_component PUBLIC cxx_std_17)
ament_target_dependencies(dialog_component rclcpp rclcpp_lifecycle
                          rclcpp_components buddy_interfaces)
rclcpp_components_register_nodes(dialog_component "DialogManagerNode")
install(
  TARGETS dialog_component
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  ament_add_gtest(test_dialog_node test/test_dialog_node.cpp
                  src/dialog_manager_node.cpp)
  target_include_directories(
    test_dialog_node
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
  target_compile_features(test_dialog_node PUBLIC cxx_std_17)
  ament_target_dependencies(test_dialog_node rclcpp rclcpp_lifecycle
                            rclcpp_components buddy_interfaces)
endif()
ament_package()
