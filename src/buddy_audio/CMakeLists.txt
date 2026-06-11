cmake_minimum_required(VERSION 3.8)
project(buddy_audio)
if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_lifecycle REQUIRED)
find_package(rclcpp_components REQUIRED)
find_package(std_msgs REQUIRED)
find_package(buddy_interfaces REQUIRED)

set(PREBUILT_BASE "${CMAKE_CURRENT_SOURCE_DIR}/../../prebuilt/current")

set(SHERPA_DIR "${PREBUILT_BASE}/sherpa-onnx")
find_path(
  SHERPA_INCLUDE
  NAMES sherpa-onnx/c-api/c-api.h
  PATHS "${SHERPA_DIR}/include"
  NO_DEFAULT_PATH
)
find_library(
  SHERPA_LIB
  NAMES sherpa-onnx-c-api
  PATHS "${SHERPA_DIR}/lib"
  NO_DEFAULT_PATH
)
find_package(ALSA REQUIRED)
find_package(CURL REQUIRED)
find_package(Boost REQUIRED)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(WEBRTC_APM QUIET webrtc-audio-processing)
endif()

# Unified ORT discovery (cpu/gpu selected by BUILD_DEVICE)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake")
include(FindORT)

# MOSS-TTS: requires GPU ORT + SentencePiece
set(SENTENCEPIECE_DIR "${PREBUILT_BASE}/sentencepiece")
find_path(
  SENTENCEPIECE_INCLUDE
  NAMES sentencepiece_processor.h
  PATHS "${SENTENCEPIECE_DIR}/include"
  NO_DEFAULT_PATH
)
find_library(
  SENTENCEPIECE_LIB
  NAMES sentencepiece
  PATHS "${SENTENCEPIECE_DIR}/lib"
  NO_DEFAULT_PATH
)

if(ORT_HAS_GPU AND SENTENCEPIECE_LIB AND SENTENCEPIECE_INCLUDE)
  set(MOSS_TTS_AVAILABLE TRUE)
  message(STATUS "MOSS-TTS: enabled (GPU ORT + SentencePiece found)")
else()
  set(MOSS_TTS_AVAILABLE FALSE)
  message(STATUS "MOSS-TTS: disabled (need BUILD_DEVICE=gpu + sentencepiece)")
endif()

# RKNN NPU backend (rk3588)
option(WITH_RKNN "Enable RKNN NPU backend for rk3588" OFF)
set(RKNN_SDK_PATH "${PREBUILT_BASE}/rknn" CACHE PATH "RKNN SDK path")
include(FindRKNN)

add_library(
  audio_component SHARED
  src/audio_config.cpp
  src/audio_pipeline_node.cpp
  src/engine_factory.cpp
  src/io/mic_capture.cpp
  src/io/audio_preprocessor.cpp
  src/speech_recognizer.cpp
  src/io/speaker_player.cpp
  src/tts/sherpa_tts_backend.cpp
  src/tts/http_tts_backend.cpp
  src/asr/sherpa_asr_backend.cpp
  src/asr/server_asr_backend.cpp
  src/runtime/infer/backend_ort.cpp
  src/runtime/infer/backend_factory.cpp
)

if(RKNN_FOUND)
  target_sources(audio_component PRIVATE
    src/runtime/infer/backend_rknn.cpp
    src/asr/asr_native_backend.cpp
    src/zipformer/pipeline.cpp
    src/melo_rknn/backend_rknn.cpp
    src/melo_rknn/frontend.cpp
    src/tts/melo_rknn_tts_backend.cpp
  )
  target_include_directories(audio_component PRIVATE ${RKNN_INCLUDE})
  target_link_libraries(audio_component ${RKNN_LIB})
  target_compile_definitions(audio_component PRIVATE HAS_RKNN=1)
else()
  target_sources(audio_component PRIVATE src/tts/melo_rknn_tts_backend_stub.cpp)
endif()

if(MOSS_TTS_AVAILABLE)
  target_sources(audio_component PRIVATE
    src/tts/moss_tts_backend.cpp
    src/moss_tts/moss_tts_pipeline.cpp
    src/moss_tts/tokenizer.cpp
    src/moss_tts/backend_onnxruntime.cpp
  )
  target_include_directories(audio_component PRIVATE ${SENTENCEPIECE_INCLUDE})
  target_link_libraries(audio_component ${SENTENCEPIECE_LIB})
  target_compile_definitions(audio_component PRIVATE MOSS_TTS_AVAILABLE=1)
  if(ORT_HAS_GPU)
    set(_ORT_RPATH_DIR "onnxruntime-gpu")
  else()
    set(_ORT_RPATH_DIR "onnxruntime")
  endif()
  set_target_properties(audio_component PROPERTIES
    BUILD_RPATH "${ORT_LIB_DIR};${SENTENCEPIECE_DIR}/lib"
    INSTALL_RPATH "$ORIGIN/../../../prebuilt/current/${_ORT_RPATH_DIR}/lib;$ORIGIN/../../../prebuilt/current/sentencepiece/lib"
  )
else()
  # Provide a stub so the linker doesn't fail
  target_sources(audio_component PRIVATE src/tts/moss_tts_backend_stub.cpp)
endif()
target_include_directories(
  audio_component PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>
                         ${SHERPA_INCLUDE} ${CURL_INCLUDE_DIRS} ${ORT_INCLUDE}
)
target_compile_features(audio_component PUBLIC cxx_std_17)
ament_target_dependencies(
  audio_component
  rclcpp
  rclcpp_lifecycle
  rclcpp_components
  std_msgs
  buddy_interfaces
)
target_link_libraries(
  audio_component
  ${SHERPA_LIB}
  ${ORT_LIB}
  ALSA::ALSA
  ${CURL_LIBRARIES}
  Boost::boost
)
if(WEBRTC_APM_FOUND)
  target_include_directories(audio_component PRIVATE ${WEBRTC_APM_INCLUDE_DIRS})
  target_link_libraries(audio_component ${WEBRTC_APM_LIBRARIES})
  target_compile_options(audio_component PRIVATE ${WEBRTC_APM_CFLAGS_OTHER})
  target_compile_definitions(audio_component PRIVATE HAS_WEBRTC_APM=1)
  message(STATUS "WebRTC APM: enabled")
else()
  message(STATUS "WebRTC APM: disabled (install libwebrtc-audio-processing-dev)")
endif()
rclcpp_components_register_nodes(audio_component "AudioPipelineNode")
install(
  TARGETS audio_component
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  ament_add_gtest(
    test_audio_node
    test/test_audio_node.cpp
    src/audio_config.cpp
    src/audio_pipeline_node.cpp
    src/engine_factory.cpp
    src/io/mic_capture.cpp
    src/io/audio_preprocessor.cpp
    src/speech_recognizer.cpp
    src/io/speaker_player.cpp
    src/tts/sherpa_tts_backend.cpp
    src/tts/http_tts_backend.cpp
    src/asr/sherpa_asr_backend.cpp
    src/asr/server_asr_backend.cpp
    src/tts/moss_tts_backend_stub.cpp
    src/tts/melo_rknn_tts_backend_stub.cpp
  )
  target_include_directories(
    test_audio_node PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> ${SHERPA_INCLUDE}
                           ${CURL_INCLUDE_DIRS}
  )
  target_compile_features(test_audio_node PUBLIC cxx_std_17)
  ament_target_dependencies(
    test_audio_node
    rclcpp
    rclcpp_lifecycle
    rclcpp_components
    buddy_interfaces
  )
  target_link_libraries(
    test_audio_node
    ${SHERPA_LIB}
    ALSA::ALSA
    ${CURL_LIBRARIES}
    Boost::boost
  )
  if(WEBRTC_APM_FOUND)
    target_include_directories(test_audio_node PRIVATE ${WEBRTC_APM_INCLUDE_DIRS})
    target_link_libraries(test_audio_node ${WEBRTC_APM_LIBRARIES})
    target_compile_options(test_audio_node PRIVATE ${WEBRTC_APM_CFLAGS_OTHER})
    target_compile_definitions(test_audio_node PRIVATE HAS_WEBRTC_APM=1)
  endif()

  ament_add_gtest(test_tts_player_interrupt test/test_tts_player_interrupt.cpp src/io/speaker_player.cpp)
  target_include_directories(test_tts_player_interrupt PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
  target_compile_features(test_tts_player_interrupt PUBLIC cxx_std_17)
  ament_target_dependencies(test_tts_player_interrupt rclcpp rclcpp_lifecycle buddy_interfaces)
  target_link_libraries(test_tts_player_interrupt ALSA::ALSA)
endif()
ament_package()
