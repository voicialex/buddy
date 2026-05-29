cmake_minimum_required(VERSION 3.8)
project(buddy_app)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_lifecycle REQUIRED)
find_package(rclcpp_components REQUIRED)
find_package(class_loader REQUIRED)
find_package(yaml_cpp_vendor REQUIRED)
find_package(yaml-cpp REQUIRED)

add_executable(buddy_main src/buddy_main.cpp src/param_file_resolver.cpp)
target_include_directories(
  buddy_main PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>
)
target_compile_features(buddy_main PUBLIC cxx_std_17)
ament_target_dependencies(
  buddy_main
  rclcpp
  rclcpp_lifecycle
  rclcpp_components
  class_loader
  yaml_cpp_vendor
)
# Link filesystem for std::filesystem Humble exports target "yaml-cpp", Jazzy exports "yaml-cpp::yaml-cpp"
if(TARGET yaml-cpp::yaml-cpp)
  target_link_libraries(buddy_main stdc++fs yaml-cpp::yaml-cpp)
else()
  target_link_libraries(buddy_main stdc++fs yaml-cpp)
endif()

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)

  ament_add_gtest(test_param_file_resolver test/test_param_file_resolver.cpp src/param_file_resolver.cpp)
  target_include_directories(test_param_file_resolver PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
  target_compile_features(test_param_file_resolver PUBLIC cxx_std_17)
endif()

install(TARGETS buddy_main DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY include/ DESTINATION include)
install(DIRECTORY params DESTINATION share/${PROJECT_NAME}/)

ament_package()
