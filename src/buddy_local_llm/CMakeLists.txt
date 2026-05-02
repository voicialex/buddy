cmake_minimum_required(VERSION 3.8)
project(buddy_local_llm)
if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_lifecycle REQUIRED)
find_package(rclcpp_components REQUIRED)
find_package(buddy_interfaces REQUIRED)
find_package(CURL REQUIRED)

add_library(local_llm_component SHARED src/local_llm_node.cpp
                                       src/ollama_client.cpp)
target_include_directories(
  local_llm_component
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:include>)
target_compile_features(local_llm_component PUBLIC cxx_std_17)
ament_target_dependencies(local_llm_component rclcpp rclcpp_lifecycle
                          rclcpp_components buddy_interfaces)
rclcpp_components_register_nodes(local_llm_component "LocalLlmNode")
target_link_libraries(local_llm_component ${CURL_LIBRARIES})
target_include_directories(local_llm_component PRIVATE ${CURL_INCLUDE_DIRS})
install(
  TARGETS local_llm_component
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  ament_add_gtest(test_local_llm_node test/test_local_llm_node.cpp
                  src/local_llm_node.cpp src/ollama_client.cpp)
  target_include_directories(
    test_local_llm_node
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
  target_compile_features(test_local_llm_node PUBLIC cxx_std_17)
  ament_target_dependencies(test_local_llm_node rclcpp rclcpp_lifecycle
                            rclcpp_components buddy_interfaces)
  target_link_libraries(test_local_llm_node ${CURL_LIBRARIES})
  target_include_directories(test_local_llm_node PRIVATE ${CURL_INCLUDE_DIRS})
endif()
ament_package()
