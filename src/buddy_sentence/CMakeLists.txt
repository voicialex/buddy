cmake_minimum_required(VERSION 3.8)
project(buddy_sentence)
if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_lifecycle REQUIRED)
find_package(rclcpp_components REQUIRED)
find_package(buddy_interfaces REQUIRED)

add_library(sentence_component SHARED src/sentence_segmenter_node.cpp)
target_include_directories(
  sentence_component
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:include>)
target_compile_features(sentence_component PUBLIC cxx_std_17)
ament_target_dependencies(sentence_component rclcpp rclcpp_lifecycle
                          rclcpp_components buddy_interfaces)
rclcpp_components_register_nodes(sentence_component "SentenceSegmenterNode")
install(
  TARGETS sentence_component
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  ament_add_gtest(test_sentence_node test/test_sentence_node.cpp
                  src/sentence_segmenter_node.cpp)
  target_include_directories(
    test_sentence_node
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
  target_compile_features(test_sentence_node PUBLIC cxx_std_17)
  ament_target_dependencies(test_sentence_node rclcpp rclcpp_lifecycle
                            rclcpp_components buddy_interfaces)
endif()
ament_package()
