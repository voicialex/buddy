cmake_minimum_required(VERSION 3.8)
project(buddy_audio)
if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_lifecycle REQUIRED)
find_package(rclcpp_components REQUIRED)
find_package(std_msgs REQUIRED)
find_package(buddy_interfaces REQUIRED)

set(SHERPA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../prebuilt/sherpa-onnx")
find_path(
  SHERPA_INCLUDE
  NAMES sherpa-onnx/c-api/c-api.h
  PATHS "${SHERPA_DIR}/include"
  NO_DEFAULT_PATH)
find_library(
  SHERPA_LIB
  NAMES sherpa-onnx-c-api
  PATHS "${SHERPA_DIR}/lib"
  NO_DEFAULT_PATH)
find_package(ALSA REQUIRED)

add_library(audio_component SHARED src/audio_pipeline_node.cpp)
target_include_directories(
  audio_component PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                         $<INSTALL_INTERFACE:include> ${SHERPA_INCLUDE})
target_compile_features(audio_component PUBLIC cxx_std_17)
ament_target_dependencies(audio_component rclcpp rclcpp_lifecycle
                          rclcpp_components std_msgs buddy_interfaces)
target_link_libraries(audio_component ${SHERPA_LIB} ALSA::ALSA)
rclcpp_components_register_nodes(audio_component "AudioPipelineNode")
install(
  TARGETS audio_component
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  ament_add_gtest(test_audio_node test/test_audio_node.cpp
                  src/audio_pipeline_node.cpp)
  target_include_directories(
    test_audio_node
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
           ${SHERPA_INCLUDE})
  target_compile_features(test_audio_node PUBLIC cxx_std_17)
  ament_target_dependencies(test_audio_node rclcpp rclcpp_lifecycle
                            rclcpp_components buddy_interfaces)
  target_link_libraries(test_audio_node ${SHERPA_LIB} ALSA::ALSA)
endif()
ament_package()
