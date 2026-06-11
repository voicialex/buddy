cmake_minimum_required(VERSION 3.8)
project(buddy_brain)
if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()
find_package(ament_cmake REQUIRED)
find_package(ament_index_cpp REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_lifecycle REQUIRED)
find_package(rclcpp_components REQUIRED)
find_package(rclcpp_action REQUIRED)
find_package(std_msgs REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(buddy_interfaces REQUIRED)

add_library(
  brain_component SHARED
  src/brain_node.cpp
  src/sentence_segmenter.cpp
  src/emotion_trigger.cpp
  src/asr_filter.cpp
  src/asr_post_filter.cpp
  src/session_context.cpp
  src/image_capture_coordinator.cpp
)
target_include_directories(
  brain_component PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>
)
target_compile_features(brain_component PUBLIC cxx_std_17)
ament_target_dependencies(
  brain_component
  ament_index_cpp
  rclcpp
  rclcpp_lifecycle
  rclcpp_components
  rclcpp_action
  std_msgs
  sensor_msgs
  buddy_interfaces
)
rclcpp_components_register_nodes(brain_component "BrainNode")
install(
  TARGETS brain_component
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)

  ament_add_gtest(
    test_brain_node
    test/test_brain_node.cpp
    src/brain_node.cpp
    src/sentence_segmenter.cpp
    src/emotion_trigger.cpp
    src/asr_filter.cpp
    src/asr_post_filter.cpp
    src/session_context.cpp
    src/image_capture_coordinator.cpp
  )
  target_include_directories(test_brain_node PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
  target_compile_features(test_brain_node PUBLIC cxx_std_17)
  ament_target_dependencies(
    test_brain_node
    rclcpp
    rclcpp_lifecycle
    rclcpp_components
    rclcpp_action
    std_msgs
    sensor_msgs
    buddy_interfaces
  )

  ament_add_gtest(test_segment test/test_segment.cpp src/sentence_segmenter.cpp)
  target_include_directories(test_segment PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
  target_compile_features(test_segment PUBLIC cxx_std_17)
  ament_target_dependencies(
    test_segment
    rclcpp
    rclcpp_lifecycle
    rclcpp_components
    rclcpp_action
    std_msgs
    sensor_msgs
    buddy_interfaces
  )

endif()
ament_package()
