# FindORT.cmake — Unified ONNX Runtime discovery
#
# Inputs:
#   BUILD_DEVICE   — "cpu", "gpu", or "npu" (passed from build.sh)
#   PREBUILT_BASE  — path to prebuilt/current (must be set before include)
#
# Outputs:
#   ORT_FOUND      — TRUE if usable ORT found
#   ORT_INCLUDE    — include directory (contains onnxruntime_cxx_api.h)
#   ORT_LIB        — library path (libonnxruntime.so)
#   ORT_HAS_GPU    — TRUE if GPU variant is linked (CUDA EP available)
#   ORT_LIB_DIR    — directory containing the .so (for RPATH)

if(NOT DEFINED PREBUILT_BASE)
  message(FATAL_ERROR "PREBUILT_BASE must be set before include(FindORT)")
endif()

set(ORT_FOUND FALSE)
set(ORT_HAS_GPU FALSE)

# GPU build: prefer onnxruntime-gpu
if(BUILD_DEVICE STREQUAL "gpu")
  set(_ORT_GPU_DIR "${PREBUILT_BASE}/onnxruntime-gpu")
  find_path(
    _ORT_GPU_INCLUDE
    NAMES onnxruntime_cxx_api.h
    PATHS "${_ORT_GPU_DIR}/include"
    NO_DEFAULT_PATH
  )
  find_library(
    _ORT_GPU_LIB
    NAMES onnxruntime
    PATHS "${_ORT_GPU_DIR}/lib"
    NO_DEFAULT_PATH
  )
  if(_ORT_GPU_INCLUDE AND _ORT_GPU_LIB)
    set(ORT_INCLUDE ${_ORT_GPU_INCLUDE})
    set(ORT_LIB ${_ORT_GPU_LIB})
    set(ORT_LIB_DIR "${_ORT_GPU_DIR}/lib")
    set(ORT_HAS_GPU TRUE)
    set(ORT_FOUND TRUE)
    message(STATUS "FindORT: using onnxruntime-gpu (CUDA EP)")
  endif()
endif()

# Fallback (or cpu/npu build): plain onnxruntime
if(NOT ORT_FOUND)
  set(_ORT_DIR "${PREBUILT_BASE}/onnxruntime")
  find_path(
    _ORT_INCLUDE
    NAMES onnxruntime_cxx_api.h
    PATHS "${_ORT_DIR}/include"
    NO_DEFAULT_PATH
  )
  find_library(
    _ORT_LIB
    NAMES onnxruntime
    PATHS "${_ORT_DIR}/lib"
    NO_DEFAULT_PATH
  )
  if(_ORT_INCLUDE AND _ORT_LIB)
    set(ORT_INCLUDE ${_ORT_INCLUDE})
    set(ORT_LIB ${_ORT_LIB})
    set(ORT_LIB_DIR "${_ORT_DIR}/lib")
    set(ORT_FOUND TRUE)
    message(STATUS "FindORT: using onnxruntime (CPU)")
  else()
    message(FATAL_ERROR "FindORT: No ONNX Runtime found in ${PREBUILT_BASE}")
  endif()
endif()
