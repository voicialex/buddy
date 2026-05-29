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
find_package(OpenCV REQUIRED)
find_package(cv_bridge REQUIRED)
find_package(image_transport REQUIRED)

# Shared cmake modules (FindORT, FindRKNN)
set(PREBUILT_BASE "${CMAKE_CURRENT_SOURCE_DIR}/../../prebuilt/current")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake")
include(FindORT)

set(RKNN_SDK_PATH "${PREBUILT_BASE}/rknn" CACHE PATH "RKNN SDK path")
include(FindRKNN)

set(VISION_SOURCES
  src/vision_pipeline_node.cpp
  src/emotion_model_haar.cpp
  src/emotion_model_retinaface.cpp
  src/infer_backend_ort.cpp
)
if(RKNN_FOUND)
  list(APPEND VISION_SOURCES src/infer_backend_rknn.cpp)
endif()

add_library(vision_component SHARED ${VISION_SOURCES})
target_include_directories(
  vision_component PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<BUILD_INTERFACE:${ORT_INCLUDE}>
                          $<INSTALL_INTERFACE:include>
)
target_compile_features(vision_component PUBLIC cxx_std_17)
ament_target_dependencies(
  vision_component
  rclcpp
  rclcpp_lifecycle
  rclcpp_components
  sensor_msgs
  buddy_interfaces
  OpenCV
  cv_bridge
  image_transport
)
target_link_libraries(vision_component ${ORT_LIB})
if(RKNN_FOUND)
  target_compile_definitions(vision_component PRIVATE HAS_RKNN=1)
  target_include_directories(vision_component PRIVATE ${RKNN_INCLUDE})
  target_link_libraries(vision_component ${RKNN_LIB})
endif()
rclcpp_components_register_nodes(vision_component "VisionPipelineNode")
install(
  TARGETS vision_component
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)

  ament_add_gtest(test_model_interface test/test_model_interface.cpp)
  target_include_directories(test_model_interface PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
  target_compile_features(test_model_interface PUBLIC cxx_std_17)
  ament_target_dependencies(test_model_interface OpenCV)

  ament_add_gtest(test_vision_node test/test_vision_node.cpp ${VISION_SOURCES})
  target_include_directories(
    test_vision_node PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                            $<BUILD_INTERFACE:${ORT_INCLUDE}>
  )
  target_compile_features(test_vision_node PUBLIC cxx_std_17)
  ament_target_dependencies(
    test_vision_node
    rclcpp
    rclcpp_lifecycle
    rclcpp_components
    sensor_msgs
    buddy_interfaces
    OpenCV
    cv_bridge
    image_transport
  )
  target_link_libraries(test_vision_node ${ORT_LIB})
  if(RKNN_FOUND)
    target_compile_definitions(test_vision_node PRIVATE HAS_RKNN=1)
    target_include_directories(test_vision_node PRIVATE ${RKNN_INCLUDE})
    target_link_libraries(test_vision_node ${RKNN_LIB})
  endif()

  ament_add_gtest(test_onnx_emotion_model test/test_onnx_emotion_model.cpp src/emotion_model_haar.cpp)
  target_include_directories(
    test_onnx_emotion_model PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                                   $<BUILD_INTERFACE:${ORT_INCLUDE}>
  )
  target_compile_features(test_onnx_emotion_model PUBLIC cxx_std_17)
  ament_target_dependencies(test_onnx_emotion_model rclcpp OpenCV)
  target_link_libraries(test_onnx_emotion_model ${ORT_LIB})
endif()
ament_package()
