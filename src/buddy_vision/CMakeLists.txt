cmake_minimum_required(VERSION 3.8)
project(buddy_vision)
if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_lifecycle REQUIRED)
find_package(rclcpp_components REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(buddy_interfaces REQUIRED)
find_package(ament_index_cpp REQUIRED)
find_package(OpenCV REQUIRED)

add_library(vision_component SHARED src/vision_pipeline_node.cpp)
target_include_directories(
  vision_component PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                          $<INSTALL_INTERFACE:include>)
target_compile_features(vision_component PUBLIC cxx_std_17)
ament_target_dependencies(vision_component rclcpp rclcpp_lifecycle
                          rclcpp_components sensor_msgs buddy_interfaces
                          ament_index_cpp OpenCV)
rclcpp_components_register_nodes(vision_component "VisionPipelineNode")
install(
  TARGETS vision_component
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin)

install(DIRECTORY models/
        DESTINATION share/${PROJECT_NAME}/models
        PATTERN ".gitignore" EXCLUDE
        PATTERN ".gitkeep" EXCLUDE)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)

  ament_add_gtest(test_frame_buffer test/test_frame_buffer.cpp)
  target_include_directories(
    test_frame_buffer
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
  target_compile_features(test_frame_buffer PUBLIC cxx_std_17)
  ament_target_dependencies(test_frame_buffer OpenCV)

  ament_add_gtest(test_model_interface test/test_model_interface.cpp)
  target_include_directories(
    test_model_interface
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
  target_compile_features(test_model_interface PUBLIC cxx_std_17)
  ament_target_dependencies(test_model_interface OpenCV)

  ament_add_gtest(test_vision_node test/test_vision_node.cpp
                  src/vision_pipeline_node.cpp)
  target_include_directories(
    test_vision_node
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
  target_compile_features(test_vision_node PUBLIC cxx_std_17)
  ament_target_dependencies(test_vision_node rclcpp rclcpp_lifecycle
                            rclcpp_components buddy_interfaces
                            ament_index_cpp OpenCV)
endif()
ament_package()
