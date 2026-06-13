# Buddy Docker build — self-contained, no external base images
# x86_64 native:  docker build --build-arg TARGET_ARCH=x86_64 ...
# arm64 cross:   docker build --build-arg TARGET_ARCH=arm64 ...
ARG VERSION=1.0.0
ARG PARALLEL_WORKERS=8
ARG DEVICE=cpu
ARG TARGET_ARCH=x86_64

# ============================================================
# Stage 1: Base environment
# ============================================================
FROM ubuntu:22.04 AS base
ARG TARGET_ARCH
ARG DEVICE
ENV DEBIAN_FRONTEND=noninteractive

# --- apt sources ---
RUN sed -i 's|http://archive.ubuntu.com|http://mirrors.aliyun.com|g' /etc/apt/sources.list

# arm64: enable multiarch
RUN if [ "$TARGET_ARCH" = "arm64" ]; then \
      dpkg --add-architecture arm64 && \
      sed -i 's|^deb |deb [arch=amd64] |g' /etc/apt/sources.list && \
      echo "deb [arch=arm64] http://ports.ubuntu.com/ubuntu-ports jammy main restricted universe multiverse" >> /etc/apt/sources.list && \
      echo "deb [arch=arm64] http://ports.ubuntu.com/ubuntu-ports jammy-updates main restricted universe multiverse" >> /etc/apt/sources.list; \
    fi

# --- native build tools + buddy runtime deps ---
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake python3-pip python3-venv python3-dev python3-numpy \
    pkg-config python3-lark python3-yaml python3-empy python3-catkin-pkg \
    libboost-dev libssl-dev \
    ccache python3-setuptools curl \
    libasound2-dev libwebrtc-audio-processing-dev \
    && rm -rf /var/lib/apt/lists/*

# arm64: cross-compiler + arm64 link-time libs
RUN if [ "$TARGET_ARCH" = "arm64" ]; then \
      apt-get update && apt-get install -y --no-install-recommends \
        gcc-aarch64-linux-gnu g++-aarch64-linux-gnu \
        libc6-dev-arm64-cross \
        libboost-dev:arm64 \
        libssl-dev:arm64 \
        libasound2-dev:arm64 libwebrtc-audio-processing-dev:arm64 \
        libcurl4:arm64 \
        libssl-dev:amd64 libcurl4-openssl-dev:amd64 \
        && rm -rf /var/lib/apt/lists/* && \
        ln -s libcurl.so.4 /usr/lib/aarch64-linux-gnu/libcurl.so && \
        mkdir -p /usr/include/aarch64-linux-gnu && \
        ln -sfn /usr/include/x86_64-linux-gnu/curl /usr/include/aarch64-linux-gnu/curl; \
    fi

# libcurl4-openssl-dev for x86_64 (arm64 uses libcurl4:arm64 + manual symlink to avoid multiarch conflict)
RUN if [ "$TARGET_ARCH" != "arm64" ]; then \
      apt-get update && apt-get install -y --no-install-recommends \
        libcurl4-openssl-dev \
        && rm -rf /var/lib/apt/lists/*; \
    fi

RUN pip3 install --no-cache-dir colcon-common-extensions vcstool 'empy<4' lark

# Toolchain file (arm64 only)
COPY docker/toolchain-aarch64.cmake /opt/toolchain-aarch64.cmake

# --- Prebuilt deps ---
# Resolve prebuilt directory name
RUN if [ "$TARGET_ARCH" = "arm64" ]; then \
      echo "aarch64" > /tmp/prebuilt_dir; \
    else \
      echo "x86_64" > /tmp/prebuilt_dir; \
    fi

COPY prebuilt/ /opt/prebuilt-src/

RUN PREBUILT_DIR="$(cat /tmp/prebuilt_dir)" && \
    mkdir -p /opt/prebuilt && \
    cp -a /opt/prebuilt-src/${PREBUILT_DIR}/ros2_core /opt/ros2_core && \
    cp -a /opt/prebuilt-src/${PREBUILT_DIR}/sherpa-onnx /opt/prebuilt/sherpa-onnx && \
    cp -a /opt/prebuilt-src/${PREBUILT_DIR}/onnxruntime /opt/prebuilt/onnxruntime && \
    cp -a /opt/prebuilt-src/${PREBUILT_DIR}/opencv /opt/prebuilt/opencv && \
    rm -rf /opt/prebuilt-src

ENV COLCON_PREFIX_PATH=/opt/ros2_core

# ============================================================
# Stage 2: Build buddy
# ============================================================
FROM base AS build
ARG TARGET_ARCH
ARG PARALLEL_WORKERS
ARG DEVICE

WORKDIR /workspace
COPY src/ src/
COPY cmake/ cmake/

# Symlink prebuilt/current to match CMakeLists convention
RUN PREBUILT_DIR="$(cat /tmp/prebuilt_dir)" && \
    mkdir -p prebuilt/${PREBUILT_DIR} && \
    ln -s /opt/prebuilt/sherpa-onnx prebuilt/${PREBUILT_DIR}/sherpa-onnx && \
    ln -s /opt/prebuilt/onnxruntime prebuilt/${PREBUILT_DIR}/onnxruntime && \
    ln -s /opt/prebuilt/opencv prebuilt/${PREBUILT_DIR}/opencv && \
    ln -s ${PREBUILT_DIR} prebuilt/current

SHELL ["/bin/bash", "-c"]
ENV CCACHE_DIR=/ccache
ENV CMAKE_PREFIX_PATH=/opt/prebuilt/opencv:/opt/prebuilt/sherpa-onnx:/opt/prebuilt/onnxruntime
RUN --mount=type=cache,target=/ccache \
    . /opt/ros2_core/setup.bash && \
    cmake_extra_flags=""; \
    if [ "$TARGET_ARCH" = "arm64" ]; then \
      cmake_extra_flags="-DCMAKE_TOOLCHAIN_FILE=/opt/toolchain-aarch64.cmake"; \
    fi && \
    if [ "$DEVICE" = "gpu" ]; then \
      cmake_extra_flags="$cmake_extra_flags -DWITH_CUDA=ON"; \
    elif [ "$DEVICE" = "npu" ]; then \
      cmake_extra_flags="$cmake_extra_flags -DWITH_RKNN=ON"; \
    fi && \
    colcon --log-base output/log \
      build \
      --build-base output/build \
      --install-base output/install \
      --parallel-workers ${PARALLEL_WORKERS} \
      --cmake-args \
        --no-warn-unused-cli \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
        -DCMAKE_C_COMPILER_LAUNCHER=ccache \
        -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
        -DBUILD_TESTING=OFF \
        -DBUILD_DEVICE=${DEVICE} \
        ${cmake_extra_flags}

# ============================================================
# Stage 3: Package deb
# ============================================================
FROM base AS package

ARG TARGET_ARCH
ARG VERSION
ARG DEVICE
WORKDIR /deb-root

# Resolve arch-specific values
RUN if [ "$TARGET_ARCH" = "arm64" ]; then \
      echo "aarch64"      > /tmp/prebuilt_dir && \
      echo "arm64"         > /tmp/deb_arch && \
      echo "aarch64-linux-gnu" > /tmp/syslib_dir; \
    else \
      echo "x86_64"        > /tmp/prebuilt_dir && \
      echo "amd64"         > /tmp/deb_arch && \
      echo "x86_64-linux-gnu" > /tmp/syslib_dir; \
    fi

RUN mkdir -p opt/buddy/bin opt/buddy/lib opt/buddy/models opt/buddy/params opt/buddy/etc opt/buddy/scripts \
            opt/buddy/services/llm \
            lib/systemd/system \
            DEBIAN

# Binaries and libraries from build
COPY --from=build /workspace/output/install/ /tmp/install/
COPY --from=build /opt/prebuilt/ /tmp/prebuilt/
COPY --from=build /opt/ros2_core/lib/ /tmp/ros2_lib/
COPY --from=build /opt/ros2_core/opt/ /tmp/ros2_opt/
COPY --from=build /opt/ros2_core/share/ /tmp/ros2_share/

# System libs needed at runtime
RUN SYSLIB="$(cat /tmp/syslib_dir)" && \
    mkdir -p /tmp/syslib && \
    for lib in libasound libcurl libssl libcrypto libnghttp2; do \
      cp /usr/lib/${SYSLIB}/${lib}.so* /tmp/syslib/ 2>/dev/null || true; \
    done

# Colcon install outputs
RUN cp /tmp/install/buddy_app/lib/buddy_app/buddy_main opt/buddy/bin/ && \
    find /tmp/install -name "*.so*" -type f -exec cp -n {} opt/buddy/lib/ \; && \
    find /tmp/install -name "*.so*" -type l -exec cp -nP {} opt/buddy/lib/ \;

# ROS 2 core libs
RUN find /tmp/ros2_lib -name "*.so*" -type f -exec cp -n {} opt/buddy/lib/ \; && \
    find /tmp/ros2_lib -name "*.so*" -type l -exec cp -nP {} opt/buddy/lib/ \; && \
    find /tmp/ros2_opt -name "*.so*" -type f -exec cp -n {} opt/buddy/lib/ \; && \
    find /tmp/ros2_opt -name "*.so*" -type l -exec cp -nP {} opt/buddy/lib/ \;

# Sherpa-ONNX
RUN cp -fP /tmp/prebuilt/sherpa-onnx/lib/*.so* opt/buddy/lib/

# ONNX Runtime
RUN if [ "$DEVICE" = "gpu" ]; then \
      cp -fP /tmp/prebuilt/onnxruntime-gpu/lib/*.so* opt/buddy/lib/ 2>/dev/null || true; \
    else \
      cp -fP /tmp/prebuilt/onnxruntime/lib/*.so* opt/buddy/lib/; \
    fi

# OpenCV
RUN cp -fP /tmp/prebuilt/opencv/lib/*.so* opt/buddy/lib/

# System libs
RUN cp -fP /tmp/syslib/* opt/buddy/lib/ 2>/dev/null || true

# RKNN (npu + arm64 only)
RUN if [ "$DEVICE" = "npu" ] && [ "$TARGET_ARCH" = "arm64" ]; then \
      cp -fP /tmp/prebuilt/rknn/lib/*.so* opt/buddy/lib/ 2>/dev/null || true; \
    fi

# Params
COPY --from=build /workspace/output/install/buddy_app/share/buddy_app/params/ opt/buddy/params/

# ROS package metadata/index (ament)
RUN mkdir -p opt/buddy/share && \
    cp -a /tmp/ros2_share/. opt/buddy/share/ && \
    for d in /tmp/install/*/share; do \
      if [ -d "$d" ]; then cp -a "$d"/. opt/buddy/share/; fi; \
    done

# arm64+npu: switch TTS to Melo RKNN by default
RUN if [ "$DEVICE" = "npu" ] && [ "$TARGET_ARCH" = "arm64" ]; then \
      tts_cfg="opt/buddy/params/audio.tts.yaml"; \
      sed -i '/^[[:space:]]*tts:/,/^[[:space:]]*server:/ s/^\([[:space:]]*engine:\).*/\1 "melo-rknn"/' "$tts_cfg" && \
      sed -i '/^[[:space:]]*tts:/,/^[[:space:]]*server:/ s/^\([[:space:]]*runtime:\).*/\1 "rknnruntime"/' "$tts_cfg"; \
    fi

# Only include minimal keyword config needed at startup
COPY src/buddy_app/params/keywords.txt opt/buddy/params/keywords.txt

# Packaging files
COPY docker/packaging/run.sh opt/buddy/run.sh
COPY docker/packaging/buddy.env opt/buddy/etc/buddy.env
COPY docker/packaging/buddy.service lib/systemd/system/buddy.service
COPY docker/packaging/control.template DEBIAN/control
COPY docker/packaging/postinst DEBIAN/postinst
COPY docker/packaging/prerm DEBIAN/prerm
COPY docker/packaging/postrm DEBIAN/postrm
COPY scripts/common.sh opt/buddy/scripts/common.sh
COPY scripts/status.sh opt/buddy/scripts/status.sh
COPY scripts/start_llm_server.sh opt/buddy/scripts/start_llm_server.sh
COPY services/llm/ opt/buddy/services/llm/

# arm64: bundle rkllm server
COPY docker/rkllm_server/ /tmp/rkllm_server/
RUN if [ "$TARGET_ARCH" = "arm64" ]; then \
      cp -a /tmp/rkllm_server/. opt/buddy/rkllm_server/; \
    else \
      mkdir -p opt/buddy/rkllm_server; \
    fi && \
    rm -rf /tmp/rkllm_server

# Keep runtime package lean
RUN rm -rf opt/buddy/services/llm/.venv \
           opt/buddy/services/llm/tests \
           opt/buddy/services/llm/__pycache__ \
           opt/buddy/services/llm/backends/__pycache__ \
           opt/buddy/rkllm_server/.venv \
           opt/buddy/rkllm_server/__pycache__

RUN chmod 755 opt/buddy/run.sh opt/buddy/bin/buddy_main \
              opt/buddy/scripts/common.sh \
              opt/buddy/scripts/status.sh \
              opt/buddy/scripts/start_llm_server.sh \
              DEBIAN/postinst DEBIAN/prerm DEBIAN/postrm && \
    sed -i "s/__VERSION__/${VERSION}/" DEBIAN/control

RUN echo "BUDDY_TARGET_ARCH=${TARGET_ARCH}" >> opt/buddy/etc/buddy.env && \
    echo "BUDDY_TARGET_DEVICE=${DEVICE}" >> opt/buddy/etc/buddy.env

RUN DEB_ARCH="$(cat /tmp/deb_arch)" && \
    mkdir -p /output && \
    dpkg-deb --build /deb-root /output/buddy-robot_${VERSION}_${DEB_ARCH}_${DEVICE}.deb

# ============================================================
# Stage 4: Models tarball
# ============================================================
FROM base AS models

ARG TARGET_ARCH
ARG VERSION
ARG DEVICE

WORKDIR /models-root
COPY models/ .

RUN rm -rf ollama/
RUN rm -f *.tar* 2>/dev/null || true

# Device-specific model filtering
RUN if [ "$DEVICE" != "npu" ]; then \
      echo "[INFO] Excluding RKNN models"; \
      rm -rf zipformer-rknn/ melo-tts-rknn/ rkllm/; \
    else \
      echo "[INFO] Excluding server-only models for npu package"; \
      rm -rf funasr-paraformer-zh-offline/ funasr-paraformer-zh-online/ funasr-vad/ ChatTTS/; \
    fi

RUN mkdir -p /output && tar czf /output/buddy-models_${VERSION}_${DEVICE}.tar.gz -C /models-root .

# ============================================================
# Export artifacts
# ============================================================
FROM scratch AS export-package
COPY --from=package /output/*.deb /

FROM scratch AS export-models
COPY --from=models /output/*.tar.gz /

FROM scratch AS export
COPY --from=package /output/*.deb /
COPY --from=models /output/*.tar.gz /
