# Buddy Docker build — multi-distro cross-compilation
# Usage:
#   Humble: docker build --target humble-export  --build-arg TARGET_ARCH=arm64 ...
#   Jazzy:  docker build --target jazzy-export   --build-arg TARGET_ARCH=arm64 ...
ARG VERSION=1.0.0
ARG PARALLEL_WORKERS=8
ARG DEVICE=cpu
ARG TARGET_ARCH=x86_64

# ============================================================
# Humble (Ubuntu 22.04, Python 3.10)
# ============================================================
FROM ubuntu:22.04 AS humble-base
ARG TARGET_ARCH
ARG DEVICE
ENV DEBIAN_FRONTEND=noninteractive
SHELL ["/bin/bash", "-c"]

# --- apt sources ---
RUN sed -i 's|http://archive.ubuntu.com|http://mirrors.aliyun.com|g' /etc/apt/sources.list

RUN if [ "$TARGET_ARCH" = "arm64" ]; then \
      dpkg --add-architecture arm64 && \
      sed -i 's|^deb |deb [arch=amd64] |g' /etc/apt/sources.list && \
      echo "deb [arch=arm64] http://mirrors.aliyun.com/ubuntu-ports jammy main restricted universe multiverse" >> /etc/apt/sources.list && \
      echo "deb [arch=arm64] http://mirrors.aliyun.com/ubuntu-ports jammy-updates main restricted universe multiverse" >> /etc/apt/sources.list; \
    fi

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
      build-essential cmake python3-pip python3-venv python3-dev python3-numpy \
      pkg-config python3-lark python3-yaml python3-empy python3-catkin-pkg \
      libboost-dev libssl-dev \
      ccache python3-setuptools curl \
      libasound2-dev libwebrtc-audio-processing-dev \
    && if [ "$TARGET_ARCH" = "arm64" ]; then \
      apt-get install -y --no-install-recommends \
        gcc-aarch64-linux-gnu g++-aarch64-linux-gnu \
        libc6-dev-arm64-cross \
        libboost-dev:arm64 \
        libpython3-dev:arm64 \
        libasound2-dev:arm64 libwebrtc-audio-processing-dev:arm64 \
        libssl-dev:arm64; \
    fi \
    && rm -rf /var/lib/apt/lists/*

RUN pip3 install --no-cache-dir colcon-common-extensions vcstool 'empy<4' lark

# Toolchain file (arm64 only)
COPY docker/toolchain-aarch64.cmake /opt/toolchain-aarch64.cmake

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
    for dep in sherpa-onnx onnxruntime opencv libcurl; do \
      cp -a /opt/prebuilt-src/${PREBUILT_DIR}/$dep /opt/prebuilt/$dep; \
    done && \
    if [ "$DEVICE" = "npu" ]; then \
      cp -a /opt/prebuilt-src/${PREBUILT_DIR}/rknn /opt/prebuilt/rknn; \
      cp -a /opt/prebuilt-src/${PREBUILT_DIR}/rkllm /opt/prebuilt/rkllm; \
    fi && \
    if [ "$DEVICE" = "gpu" ]; then \
      cp -a /opt/prebuilt-src/${PREBUILT_DIR}/onnxruntime-gpu /opt/prebuilt/onnxruntime-gpu; \
    fi && \
    rm -rf /opt/prebuilt-src

ENV COLCON_PREFIX_PATH=/opt/ros2_core
ENV PYTHON_VER=3.10

# ============================================================
# Jazzy (Ubuntu 24.04, Python 3.12)
# ============================================================
FROM ubuntu:24.04 AS jazzy-base
ARG TARGET_ARCH
ARG DEVICE
ENV DEBIAN_FRONTEND=noninteractive
SHELL ["/bin/bash", "-c"]

# --- apt sources (24.04 deb822 format) ---
RUN sed -i 's|http://archive.ubuntu.com|http://mirrors.aliyun.com|g' /etc/apt/sources.list.d/ubuntu.sources \
    && dpkg --add-architecture arm64 \
    && echo "deb [arch=arm64] http://mirrors.aliyun.com/ubuntu-ports noble main restricted universe multiverse" > /etc/apt/sources.list.d/arm64.list \
    && echo "deb [arch=arm64] http://mirrors.aliyun.com/ubuntu-ports noble-updates main restricted universe multiverse" >> /etc/apt/sources.list.d/arm64.list \
    && sed -i 's|^Types: deb|Types: deb\nArchitectures: amd64|' /etc/apt/sources.list.d/ubuntu.sources

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
      build-essential cmake python3-pip python3-venv python3-dev python3-numpy pkg-config \
      python3-lark python3-yaml python3-empy python3-catkin-pkg \
      libboost-dev libssl-dev \
      ccache python3-setuptools curl \
      libasound2-dev libwebrtc-audio-processing-dev \
    && if [ "$TARGET_ARCH" = "arm64" ]; then \
      apt-get install -y --no-install-recommends \
        gcc-aarch64-linux-gnu g++-aarch64-linux-gnu \
        libboost-dev:arm64 \
        libssl-dev:arm64 \
        libpython3-dev:arm64 \
        libyaml-cpp-dev:arm64 \
        libasound2-dev:arm64 libwebrtc-audio-processing-dev:arm64; \
    fi \
    && rm -rf /var/lib/apt/lists/* \
    && rm -f /usr/lib/python3*/EXTERNALLY-MANAGED

RUN pip3 install --break-system-packages --no-cache-dir colcon-common-extensions vcstool

# Toolchain file (arm64 only)
COPY docker/toolchain-aarch64.cmake /opt/toolchain-aarch64.cmake

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
    for dep in sherpa-onnx onnxruntime opencv libcurl; do \
      cp -a /opt/prebuilt-src/${PREBUILT_DIR}/$dep /opt/prebuilt/$dep; \
    done && \
    if [ "$DEVICE" = "npu" ]; then \
      cp -a /opt/prebuilt-src/${PREBUILT_DIR}/rknn /opt/prebuilt/rknn; \
      cp -a /opt/prebuilt-src/${PREBUILT_DIR}/rkllm /opt/prebuilt/rkllm; \
    fi && \
    if [ "$DEVICE" = "gpu" ]; then \
      cp -a /opt/prebuilt-src/${PREBUILT_DIR}/onnxruntime-gpu /opt/prebuilt/onnxruntime-gpu; \
    fi && \
    rm -rf /opt/prebuilt-src

ENV COLCON_PREFIX_PATH=/opt/ros2_core
ENV PYTHON_VER=3.12

# ============================================================
# Humble build + package chain
# ============================================================
FROM humble-base AS humble-build
ARG TARGET_ARCH
ARG PARALLEL_WORKERS
ARG DEVICE

WORKDIR /workspace
COPY src/ src/
COPY cmake/ cmake/

RUN PREBUILT_DIR="$(cat /tmp/prebuilt_dir)" && \
    mkdir -p prebuilt/${PREBUILT_DIR} && \
    for dep in sherpa-onnx onnxruntime opencv libcurl; do \
      ln -s /opt/prebuilt/$dep prebuilt/${PREBUILT_DIR}/$dep; \
    done && \
    if [ "$DEVICE" = "npu" ]; then \
      ln -s /opt/prebuilt/rknn prebuilt/${PREBUILT_DIR}/rknn; \
    fi && \
    ln -s ${PREBUILT_DIR} prebuilt/current

ENV CCACHE_DIR=/ccache
ENV CMAKE_PREFIX_PATH=/opt/prebuilt/opencv:/opt/prebuilt/sherpa-onnx:/opt/prebuilt/onnxruntime:/opt/prebuilt/libcurl
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

# --- Humble Package ---
FROM humble-base AS humble-package
ARG TARGET_ARCH
ARG VERSION
ARG DEVICE
WORKDIR /deb-root

RUN if [ "$TARGET_ARCH" = "arm64" ]; then \
      echo "aarch64"      > /tmp/prebuilt_dir && \
      echo "arm64"         > /tmp/deb_arch; \
    else \
      echo "x86_64"        > /tmp/prebuilt_dir && \
      echo "amd64"         > /tmp/deb_arch; \
    fi

RUN mkdir -p opt/buddy/bin opt/buddy/lib opt/buddy/models opt/buddy/params opt/buddy/etc opt/buddy/scripts \
            opt/buddy/services/llm \
            lib/systemd/system \
            DEBIAN

COPY --from=humble-build /workspace/output/install/ /tmp/install/
COPY --from=humble-build /opt/prebuilt/ /tmp/prebuilt/
COPY --from=humble-build /opt/ros2_core/lib/ /tmp/ros2_lib/
COPY --from=humble-build /opt/ros2_core/opt/ /tmp/ros2_opt/
COPY --from=humble-build /opt/ros2_core/share/ /tmp/ros2_share/
COPY --from=humble-build /opt/ros2_core/bin/ /tmp/ros2_bin/
COPY --from=humble-build /opt/ros2_core/lib/python3.10/ /tmp/ros2_python/

RUN cp /tmp/install/buddy_app/lib/buddy_app/buddy_main opt/buddy/bin/ && \
    find /tmp/install /tmp/ros2_lib /tmp/ros2_opt -name "*.so*" -type f -exec cp -n {} opt/buddy/lib/ \; && \
    find /tmp/install /tmp/ros2_lib /tmp/ros2_opt -name "*.so*" -type l -exec cp -nP {} opt/buddy/lib/ \; && \
    for dep in sherpa-onnx onnxruntime opencv libcurl; do \
      cp -fP /tmp/prebuilt/$dep/lib/*.so* opt/buddy/lib/; \
    done && \
    if [ "$DEVICE" = "gpu" ]; then \
      cp -fP /tmp/prebuilt/onnxruntime-gpu/lib/*.so* opt/buddy/lib/ 2>/dev/null || true; \
    fi && \
    if [ "$DEVICE" = "npu" ] && [ "$TARGET_ARCH" = "arm64" ]; then \
      cp -fP /tmp/prebuilt/rknn/lib/*.so* opt/buddy/lib/ 2>/dev/null || true; \
    fi

COPY --from=humble-build /workspace/output/install/buddy_app/share/buddy_app/params/ opt/buddy/params/

RUN mkdir -p opt/buddy/share && \
    cp -a /tmp/ros2_share/. opt/buddy/share/ && \
    for d in /tmp/install/*/share; do \
      if [ -d "$d" ]; then cp -a "$d"/. opt/buddy/share/; fi; \
    done

COPY --from=humble-build /opt/ros2_core/local/lib/python3.10/ /tmp/ros2_local_python/
RUN cp /tmp/ros2_bin/ros2 opt/buddy/bin/_ros2_entry && \
    cp /tmp/ros2_bin/_ros2_daemon opt/buddy/bin/ 2>/dev/null || true && \
    chmod 755 opt/buddy/bin/_ros2_entry && \
    mkdir -p opt/buddy/lib/python3.10 && \
    cp -a /tmp/ros2_python/. opt/buddy/lib/python3.10/ && \
    cp -a /tmp/ros2_local_python/. opt/buddy/lib/python3.10/ && \
    if [ "$TARGET_ARCH" = "arm64" ]; then \
      find opt/buddy/lib/python3.10/ -name "*x86_64-linux-gnu*" -exec rename 's/x86_64-linux-gnu/aarch64-linux-gnu/' {} + 2>/dev/null || \
      find opt/buddy/lib/python3.10/ -name "*x86_64-linux-gnu*" | while read f; do mv "$f" "$(echo "$f" | sed 's/x86_64-linux-gnu/aarch64-linux-gnu/')"; done; \
    fi

COPY src/buddy_app/params/keywords.txt opt/buddy/params/keywords.txt
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

COPY docker/buildvenv.sh docker/buildvenv.sh
COPY docker/activate.sh docker/activate.sh
RUN BUDDY_PYTHON_VER=3.10 bash docker/buildvenv.sh && rm docker/buildvenv.sh docker/activate.sh

COPY docker/rkllm_server/ /tmp/rkllm_server/
RUN if [ "$TARGET_ARCH" = "arm64" ]; then \
      cp -a /tmp/rkllm_server/. opt/buddy/rkllm_server/; \
    else \
      mkdir -p opt/buddy/rkllm_server; \
    fi && \
    rm -rf /tmp/rkllm_server \
           opt/buddy/services/llm/tests \
           opt/buddy/services/llm/__pycache__ \
           opt/buddy/services/llm/backends/__pycache__ \
           opt/buddy/rkllm_server/__pycache__ && \
    python3.10 -m venv opt/buddy/rkllm_server/.venv && \
    opt/buddy/rkllm_server/.venv/bin/pip install --no-cache-dir flask && \
    sha256sum opt/buddy/rkllm_server/requirements.txt | awk '{print $1}' > opt/buddy/rkllm_server/.venv/.requirements.sha256 && \
    touch opt/buddy/rkllm_server/.venv/.prebuilt && \
    if [ "$TARGET_ARCH" = "arm64" ]; then \
      mkdir -p opt/buddy/rkllm_server/lib && \
      cp -a /opt/prebuilt/rkllm/lib/*.so* opt/buddy/rkllm_server/lib/; \
    fi && \
    chmod 755 opt/buddy/run.sh opt/buddy/bin/buddy_main \
              opt/buddy/scripts/common.sh \
              opt/buddy/scripts/status.sh \
              opt/buddy/scripts/start_llm_server.sh \
              DEBIAN/postinst DEBIAN/prerm DEBIAN/postrm && \
    sed -i "s/__VERSION__/${VERSION}/" DEBIAN/control && \
    echo "BUDDY_TARGET_ARCH=${TARGET_ARCH}" >> opt/buddy/etc/buddy.env && \
    echo "BUDDY_TARGET_DEVICE=${DEVICE}" >> opt/buddy/etc/buddy.env && \
    echo "BUDDY_ROS2_DISTRO=humble" >> opt/buddy/etc/buddy.env

COPY docker/packaging/ros2.sh opt/buddy/bin/ros2
COPY docker/packaging/setup.bash opt/buddy/setup.bash
RUN chmod 755 opt/buddy/bin/ros2 opt/buddy/setup.bash

RUN DEB_ARCH="$(cat /tmp/deb_arch)" && \
    mkdir -p /output && \
    dpkg-deb --build /deb-root /output/buddy-robot_${VERSION}_${DEB_ARCH}_${DEVICE}.deb

# --- Humble Models ---
FROM humble-base AS humble-models
ARG TARGET_ARCH
ARG VERSION
ARG DEVICE

WORKDIR /models-root
COPY models/ .

RUN rm -rf ollama/ && \
    rm -f *.tar* 2>/dev/null || true && \
    if [ "$DEVICE" != "npu" ]; then \
      echo "[INFO] Excluding RKNN models"; \
      rm -rf zipformer-rknn/ melo-tts-rknn/ rkllm/; \
    else \
      echo "[INFO] Excluding server-only models for npu package"; \
      rm -rf funasr-paraformer-zh-offline/ funasr-paraformer-zh-online/ funasr-vad/ ChatTTS/; \
    fi && \
    mkdir -p /output && tar czf /output/buddy-models_${VERSION}_${DEVICE}.tar.gz -C /models-root .

# --- Humble Exports ---
FROM scratch AS humble-export-package
COPY --from=humble-package /output/*.deb /

FROM scratch AS humble-export-models
COPY --from=humble-models /output/*.tar.gz /

FROM scratch AS humble-export
COPY --from=humble-package /output/*.deb /
COPY --from=humble-models /output/*.tar.gz /

# ============================================================
# Jazzy build + package chain
# ============================================================
FROM jazzy-base AS jazzy-build
ARG TARGET_ARCH
ARG PARALLEL_WORKERS
ARG DEVICE

WORKDIR /workspace
COPY src/ src/
COPY cmake/ cmake/

RUN PREBUILT_DIR="$(cat /tmp/prebuilt_dir)" && \
    mkdir -p prebuilt/${PREBUILT_DIR} && \
    for dep in sherpa-onnx onnxruntime opencv libcurl; do \
      ln -s /opt/prebuilt/$dep prebuilt/${PREBUILT_DIR}/$dep; \
    done && \
    if [ "$DEVICE" = "npu" ]; then \
      ln -s /opt/prebuilt/rknn prebuilt/${PREBUILT_DIR}/rknn; \
    fi && \
    ln -s ${PREBUILT_DIR} prebuilt/current

ENV CCACHE_DIR=/ccache
ENV CMAKE_PREFIX_PATH=/opt/prebuilt/opencv:/opt/prebuilt/sherpa-onnx:/opt/prebuilt/onnxruntime:/opt/prebuilt/libcurl
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

# Ensure directory exists for COPY (jazzy may not have local/lib/)
RUN mkdir -p /opt/ros2_core/local/lib/python3.12

# --- Jazzy Package ---
FROM jazzy-base AS jazzy-package
ARG TARGET_ARCH
ARG VERSION
ARG DEVICE
WORKDIR /deb-root

RUN if [ "$TARGET_ARCH" = "arm64" ]; then \
      echo "aarch64"      > /tmp/prebuilt_dir && \
      echo "arm64"         > /tmp/deb_arch; \
    else \
      echo "x86_64"        > /tmp/prebuilt_dir && \
      echo "amd64"         > /tmp/deb_arch; \
    fi

RUN mkdir -p opt/buddy/bin opt/buddy/lib opt/buddy/models opt/buddy/params opt/buddy/etc opt/buddy/scripts \
            opt/buddy/services/llm \
            lib/systemd/system \
            DEBIAN

COPY --from=jazzy-build /workspace/output/install/ /tmp/install/
COPY --from=jazzy-build /opt/prebuilt/ /tmp/prebuilt/
COPY --from=jazzy-build /opt/ros2_core/lib/ /tmp/ros2_lib/
COPY --from=jazzy-build /opt/ros2_core/opt/ /tmp/ros2_opt/
COPY --from=jazzy-build /opt/ros2_core/share/ /tmp/ros2_share/
COPY --from=jazzy-build /opt/ros2_core/bin/ /tmp/ros2_bin/
COPY --from=jazzy-build /opt/ros2_core/lib/python3.12/ /tmp/ros2_python/

RUN cp /tmp/install/buddy_app/lib/buddy_app/buddy_main opt/buddy/bin/ && \
    find /tmp/install /tmp/ros2_lib /tmp/ros2_opt -name "*.so*" -type f -exec cp -n {} opt/buddy/lib/ \; && \
    find /tmp/install /tmp/ros2_lib /tmp/ros2_opt -name "*.so*" -type l -exec cp -nP {} opt/buddy/lib/ \; && \
    for dep in sherpa-onnx onnxruntime opencv libcurl; do \
      cp -fP /tmp/prebuilt/$dep/lib/*.so* opt/buddy/lib/; \
    done && \
    if [ "$DEVICE" = "gpu" ]; then \
      cp -fP /tmp/prebuilt/onnxruntime-gpu/lib/*.so* opt/buddy/lib/ 2>/dev/null || true; \
    fi && \
    if [ "$DEVICE" = "npu" ] && [ "$TARGET_ARCH" = "arm64" ]; then \
      cp -fP /tmp/prebuilt/rknn/lib/*.so* opt/buddy/lib/ 2>/dev/null || true; \
    fi

COPY --from=jazzy-build /workspace/output/install/buddy_app/share/buddy_app/params/ opt/buddy/params/

RUN mkdir -p opt/buddy/share && \
    cp -a /tmp/ros2_share/. opt/buddy/share/ && \
    for d in /tmp/install/*/share; do \
      if [ -d "$d" ]; then cp -a "$d"/. opt/buddy/share/; fi; \
    done

COPY --from=jazzy-build /opt/ros2_core/local/lib/python3.12/ /tmp/ros2_local_python/
RUN cp /tmp/ros2_bin/ros2 opt/buddy/bin/_ros2_entry && \
    cp /tmp/ros2_bin/_ros2_daemon opt/buddy/bin/ 2>/dev/null || true && \
    chmod 755 opt/buddy/bin/_ros2_entry && \
    mkdir -p opt/buddy/lib/python3.12 && \
    cp -a /tmp/ros2_python/. opt/buddy/lib/python3.12/ && \
    cp -a /tmp/ros2_local_python/. opt/buddy/lib/python3.12/ && \
    if [ "$TARGET_ARCH" = "arm64" ]; then \
      find opt/buddy/lib/python3.12/ -name "*x86_64-linux-gnu*" -exec rename 's/x86_64-linux-gnu/aarch64-linux-gnu/' {} + 2>/dev/null || \
      find opt/buddy/lib/python3.12/ -name "*x86_64-linux-gnu*" | while read f; do mv "$f" "$(echo "$f" | sed 's/x86_64-linux-gnu/aarch64-linux-gnu/')"; done; \
    fi

COPY src/buddy_app/params/keywords.txt opt/buddy/params/keywords.txt
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

COPY docker/buildvenv.sh docker/buildvenv.sh
COPY docker/activate.sh docker/activate.sh
RUN BUDDY_PYTHON_VER=3.12 bash docker/buildvenv.sh && rm docker/buildvenv.sh docker/activate.sh

COPY docker/rkllm_server/ /tmp/rkllm_server/
RUN if [ "$TARGET_ARCH" = "arm64" ]; then \
      cp -a /tmp/rkllm_server/. opt/buddy/rkllm_server/; \
    else \
      mkdir -p opt/buddy/rkllm_server; \
    fi && \
    rm -rf /tmp/rkllm_server \
           opt/buddy/services/llm/tests \
           opt/buddy/services/llm/__pycache__ \
           opt/buddy/services/llm/backends/__pycache__ \
           opt/buddy/rkllm_server/__pycache__ && \
    python3.12 -m venv opt/buddy/rkllm_server/.venv && \
    opt/buddy/rkllm_server/.venv/bin/pip install --no-cache-dir flask && \
    sha256sum opt/buddy/rkllm_server/requirements.txt | awk '{print $1}' > opt/buddy/rkllm_server/.venv/.requirements.sha256 && \
    touch opt/buddy/rkllm_server/.venv/.prebuilt && \
    chmod 755 opt/buddy/run.sh opt/buddy/bin/buddy_main \
              opt/buddy/scripts/common.sh \
              opt/buddy/scripts/status.sh \
              opt/buddy/scripts/start_llm_server.sh \
              DEBIAN/postinst DEBIAN/prerm DEBIAN/postrm && \
    sed -i "s/__VERSION__/${VERSION}/" DEBIAN/control && \
    echo "BUDDY_TARGET_ARCH=${TARGET_ARCH}" >> opt/buddy/etc/buddy.env && \
    echo "BUDDY_TARGET_DEVICE=${DEVICE}" >> opt/buddy/etc/buddy.env && \
    echo "BUDDY_ROS2_DISTRO=jazzy" >> opt/buddy/etc/buddy.env

COPY docker/packaging/ros2.sh opt/buddy/bin/ros2
COPY docker/packaging/setup.bash opt/buddy/setup.bash
RUN chmod 755 opt/buddy/bin/ros2 opt/buddy/setup.bash

RUN DEB_ARCH="$(cat /tmp/deb_arch)" && \
    mkdir -p /output && \
    dpkg-deb --build /deb-root /output/buddy-robot_${VERSION}_${DEB_ARCH}_${DEVICE}.deb

# --- Jazzy Models ---
FROM jazzy-base AS jazzy-models
ARG TARGET_ARCH
ARG VERSION
ARG DEVICE

WORKDIR /models-root
COPY models/ .

RUN rm -rf ollama/ && \
    rm -f *.tar* 2>/dev/null || true && \
    if [ "$DEVICE" != "npu" ]; then \
      echo "[INFO] Excluding RKNN models"; \
      rm -rf zipformer-rknn/ melo-tts-rknn/ rkllm/; \
    else \
      echo "[INFO] Excluding server-only models for npu package"; \
      rm -rf funasr-paraformer-zh-offline/ funasr-paraformer-zh-online/ funasr-vad/ ChatTTS/; \
    fi && \
    mkdir -p /output && tar czf /output/buddy-models_${VERSION}_${DEVICE}.tar.gz -C /models-root .

# --- Jazzy Exports ---
FROM scratch AS jazzy-export-package
COPY --from=jazzy-package /output/*.deb /

FROM scratch AS jazzy-export-models
COPY --from=jazzy-models /output/*.tar.gz /

FROM scratch AS jazzy-export
COPY --from=jazzy-package /output/*.deb /
COPY --from=jazzy-models /output/*.tar.gz /
