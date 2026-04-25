cmake_minimum_required(VERSION 3.8)
project(buddy_cloud)
if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_lifecycle REQUIRED)
find_package(rclcpp_components REQUIRED)
find_package(buddy_interfaces REQUIRED)

add_library(${PROJECT_NAME}_component SHARED src/cloud_client_node.cpp)
target_include_directories(${PROJECT_NAME}_component PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_compile_features(${PROJECT_NAME}_component PUBLIC cxx_std_17)
ament_target_dependencies(${PROJECT_NAME}_component
  rclcpp rclcpp_lifecycle rclcpp_components buddy_interfaces)

rclcpp_components_register_node(
  ${PROJECT_NAME}_component
  PLUGIN "CloudClientNode"
  EXECUTABLE cloud_node
)

install(TARGETS ${PROJECT_NAME}_component
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  ament_add_gtest(test_cloud_node test/test_cloud_node.cpp src/cloud_client_node.cpp)
  target_include_directories(test_cloud_node PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
  target_compile_features(test_cloud_node PUBLIC cxx_std_17)
  ament_target_dependencies(test_cloud_node rclcpp rclcpp_lifecycle rclcpp_components buddy_interfaces)
  target_compile_definitions(test_cloud_node PRIVATE BUDDY_UNIT_TEST)
endif()
ament_package()
