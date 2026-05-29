# FindRKNN.cmake — locate RKNN SDK for rk3576/rk3588 NPU inference
#
# Input variables:
#   WITH_RKNN       - option to enable (default OFF)
#   RKNN_SDK_PATH   - path to RKNN SDK root (default: prebuilt/current/rknn)
#
# Output variables (set only when found):
#   RKNN_FOUND      - TRUE if RKNN SDK is usable
#   RKNN_INCLUDE    - include directory containing rknn_api.h
#   RKNN_LIB        - path to librknnrt.so
#
# Usage in consuming CMakeLists.txt:
#   list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake")
#   option(WITH_RKNN "Enable RKNN NPU backend" OFF)
#   set(RKNN_SDK_PATH "${PREBUILT_BASE}/rknn" CACHE PATH "RKNN SDK")
#   include(FindRKNN)
#
#   if(RKNN_FOUND)
#     target_sources(my_lib PRIVATE src/backend_rknn.cpp)
#     target_include_directories(my_lib PRIVATE ${RKNN_INCLUDE})
#     target_link_libraries(my_lib ${RKNN_LIB})
#     target_compile_definitions(my_lib PRIVATE HAS_RKNN=1)
#   endif()

set(RKNN_FOUND FALSE)

if(NOT WITH_RKNN)
  return()
endif()

if(NOT RKNN_SDK_PATH)
  set(RKNN_SDK_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../prebuilt/current/rknn")
endif()

# Try two include layouts: flat (rknn_api.h) and namespaced (rknnrt/rknn_api.h)
find_path(RKNN_INCLUDE
  NAMES rknn_api.h rknnrt/rknn_api.h
  PATHS "${RKNN_SDK_PATH}/include"
  NO_DEFAULT_PATH
)

find_library(RKNN_LIB
  NAMES rknnrt
  PATHS "${RKNN_SDK_PATH}/lib"
  NO_DEFAULT_PATH
)

if(RKNN_INCLUDE AND RKNN_LIB)
  set(RKNN_FOUND TRUE)
  message(STATUS "RKNN: found (${RKNN_LIB})")
else()
  message(WARNING "WITH_RKNN=ON but RKNN SDK not found at ${RKNN_SDK_PATH}, disabling")
  set(WITH_RKNN OFF PARENT_SCOPE)
endif()
