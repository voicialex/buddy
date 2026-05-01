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

# ONNX Runtime (pre-built in prebuilt/ at workspace root)
set(ORT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../prebuilt/onnxruntime")
find_path(ORT_INCLUDE_DIR
  NAMES onnxruntime_cxx_api.h
  PATHS "${ORT_DIR}/include"
  NO_DEFAULT_PATH)
find_library(ORT_LIBRARY
  NAMES onnxruntime
  PATHS "${ORT_DIR}/lib"
  NO_DEFAULT_PATH)
if(NOT ORT_INCLUDE_DIR OR NOT ORT_LIBRARY)
  message(FATAL_ERROR "ONNX Runtime not found in prebuilt/onnxruntime/")
endif()
message(STATUS "ONNX Runtime: ${ORT_LIBRARY}")

add_library(vision_component SHARED
  src/vision_pipeline_node.cpp
  src/onnx_emotion_model.cpp)
target_include_directories(
  vision_component PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<BUILD_INTERFACE:${ORT_INCLUDE_DIR}>
  $<INSTALL_INTERFACE:include>)
target_compile_features(vision_component PUBLIC cxx_std_17)
ament_target_dependencies(vision_component rclcpp rclcpp_lifecycle
                          rclcpp_components sensor_msgs buddy_interfaces
                          ament_index_cpp OpenCV)
target_link_libraries(vision_component ${ORT_LIBRARY})
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
                  src/vision_pipeline_node.cpp
                  src/onnx_emotion_model.cpp)
  target_include_directories(
    test_vision_node
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
           $<BUILD_INTERFACE:${ORT_INCLUDE_DIR}>)
  target_compile_features(test_vision_node PUBLIC cxx_std_17)
  ament_target_dependencies(test_vision_node rclcpp rclcpp_lifecycle
                            rclcpp_components buddy_interfaces
                            ament_index_cpp OpenCV)
  target_link_libraries(test_vision_node ${ORT_LIBRARY})

  ament_add_gtest(test_onnx_emotion_model test/test_onnx_emotion_model.cpp
                  src/onnx_emotion_model.cpp)
  target_include_directories(
    test_onnx_emotion_model
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
           $<BUILD_INTERFACE:${ORT_INCLUDE_DIR}>)
  target_compile_features(test_onnx_emotion_model PUBLIC cxx_std_17)
  ament_target_dependencies(test_onnx_emotion_model OpenCV)
  target_link_libraries(test_onnx_emotion_model ${ORT_LIBRARY})
endif()
ament_package()
