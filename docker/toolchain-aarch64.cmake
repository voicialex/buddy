# CMake toolchain file for cross-compiling to aarch64 (Ubuntu multiarch)
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)

# Tell cmake this is aarch64 multiarch — FindXXX modules use this
# to search /usr/lib/aarch64-linux-gnu/ automatically
set(CMAKE_LIBRARY_ARCHITECTURE "aarch64-linux-gnu")

# Search prebuilt arm64 deps
set(CMAKE_PREFIX_PATH
    /opt/ros2_core
    /opt/prebuilt/onnxruntime
    /opt/prebuilt/sherpa-onnx
    /opt/opencv
)

# Don't restrict find_path/find_library — multiarch puts arm64 libs
# under /usr/lib/aarch64-linux-gnu which is not a separate sysroot
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)

# pkg-config for arm64
set(ENV{PKG_CONFIG_PATH} "/usr/lib/aarch64-linux-gnu/pkgconfig")
set(ENV{PKG_CONFIG_LIBDIR} "/usr/lib/aarch64-linux-gnu/pkgconfig")
