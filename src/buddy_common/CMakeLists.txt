cmake_minimum_required(VERSION 3.8)
project(buddy_common)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(CURL REQUIRED)

add_library(buddy_common SHARED src/http_client.cpp)
target_include_directories(
  buddy_common PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_compile_features(buddy_common PUBLIC cxx_std_17)
target_link_libraries(buddy_common ${CURL_LIBRARIES})
target_include_directories(buddy_common PRIVATE ${CURL_INCLUDE_DIRS})

install(
  TARGETS buddy_common
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)

install(
  DIRECTORY include/
  DESTINATION include
)

ament_export_targets(buddy_commonTargets HAS_LIBRARY_TARGET)
ament_export_include_directories(include)
ament_export_libraries(buddy_common)
ament_export_dependencies(CURL)

install(
  TARGETS buddy_common
  EXPORT buddy_commonTargets
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)

ament_package()
