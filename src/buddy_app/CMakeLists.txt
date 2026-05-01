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

add_executable(buddy_main src/buddy_main.cpp)
target_compile_features(buddy_main PUBLIC cxx_std_17)
ament_target_dependencies(buddy_main rclcpp rclcpp_lifecycle rclcpp_components
                          class_loader)
# Link filesystem for std::filesystem
target_link_libraries(buddy_main stdc++fs yaml-cpp)

install(TARGETS buddy_main DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY params DESTINATION share/${PROJECT_NAME}/)

ament_package()
