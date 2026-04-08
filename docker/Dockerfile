FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

# ── Pinned dependency versions ───────────────────────────────────────

ARG VCPKG_TAG=2026.03.18
ARG ADS_TAG=4.5.0
ARG ROCKY_TAG=v1.0.0
ARG VSGQT_TAG=v0.4.0

# ── System packages ──────────────────────────────────────────────────

RUN apt-get update && apt-get install -y --no-install-recommends \
    cmake build-essential ninja-build git curl zip unzip tar pkg-config \
    ca-certificates \
    qt6-base-dev qt6-base-private-dev qt6-tools-dev libqt6svg6-dev \
    qt6-declarative-dev \
    libvulkan-dev vulkan-validationlayers \
    libxcb1-dev libxcb-keysyms1-dev libxcb-image0-dev libxcb-shm0-dev \
    libxcb-icccm4-dev libxcb-sync-dev libxcb-xfixes0-dev libxcb-shape0-dev \
    libxcb-randr0-dev libxcb-render-util0-dev libxcb-util-dev libxcb-xkb-dev \
    libxcb-xinerama0-dev libxcb-cursor-dev libxkbcommon-dev \
    libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev \
    libgl1-mesa-dev libglu1-mesa-dev \
    autoconf autoconf-archive automake libtool \
    bison flex \
    python3 \
    libgtest-dev \
    && rm -rf /var/lib/apt/lists/*

# System pkg-config path for the host architecture
RUN echo "PKG_CONFIG_PATH=/usr/lib/$(dpkg-architecture -qDEB_HOST_MULTIARCH)/pkgconfig:/usr/share/pkgconfig" >> /etc/environment
ENV PKG_CONFIG_PATH="/usr/lib/aarch64-linux-gnu/pkgconfig:/usr/share/pkgconfig"

# Detect vcpkg triplet from host arch
RUN ARCH=$(uname -m) && \
    if [ "$ARCH" = "x86_64" ]; then echo "x64-linux" > /tmp/vcpkg-triplet; \
    elif [ "$ARCH" = "aarch64" ]; then echo "arm64-linux" > /tmp/vcpkg-triplet; \
    else echo "$ARCH-linux" > /tmp/vcpkg-triplet; fi

WORKDIR /opt

# ── vcpkg ────────────────────────────────────────────────────────────

RUN git clone --branch $VCPKG_TAG --depth 1 https://github.com/microsoft/vcpkg.git \
    && cd vcpkg && ./bootstrap-vcpkg.sh -disableMetrics

ENV VCPKG_DIR=/opt/vcpkg

# ── Qt Advanced Docking System ───────────────────────────────────────

RUN git clone --branch $ADS_TAG https://github.com/githubuser0xFFFF/Qt-Advanced-Docking-System.git \
    && cd Qt-Advanced-Docking-System \
    && cmake -B build -G Ninja \
        -DCMAKE_INSTALL_PREFIX=/opt/local \
        -DBUILD_EXAMPLES=OFF \
        -DBUILD_STATIC=OFF \
        -DQT_VERSION_MAJOR=6 \
    && ninja -C build -j$(nproc) \
    && ninja -C build install

# ── Rocky (with vcpkg dependencies) ─────────────────────────────────

# Build Rocky without Qt demo (vsgQt not available yet)
# ImGui enabled (the default) to avoid source patches
RUN git clone --branch $ROCKY_TAG --depth 1 https://github.com/pelicanmapping/rocky.git \
    && cd rocky \
    && cmake -B build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE=$VCPKG_DIR/scripts/buildsystems/vcpkg.cmake \
        -DVCPKG_MANIFEST_DIR=/opt/rocky \
        -DROCKY_SUPPORTS_QT=OFF \
        -DCMAKE_INSTALL_PREFIX=/opt/local \
    && ninja -C build -j$(nproc) \
    && ninja -C build install

# ── vsgQt ────────────────────────────────────────────────────────────

RUN TRIPLET=$(cat /tmp/vcpkg-triplet) && \
    cd /opt && git clone --branch $VSGQT_TAG --depth 1 https://github.com/vsg-dev/vsgQt.git \
    && cd vsgQt \
    && cmake -B build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_PREFIX_PATH="/opt/rocky/build/vcpkg_installed/$TRIPLET" \
        -DCMAKE_INSTALL_PREFIX=/opt/local \
        -DQT_PACKAGE_NAME=Qt6 \
        -DVSGQT_BUILD_EXAMPLES=OFF \
    && ninja -C build -j$(nproc) \
    && ninja -C build install

# ── Build and test the project ───────────────────────────────────────

WORKDIR /src
COPY . /src/
# Ensure data directory is available at build time for PROJECT_SOURCE_DIR fallback

RUN TRIPLET=$(cat /tmp/vcpkg-triplet) && \
    cmake -B build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_PREFIX_PATH="/opt/local;/opt/rocky/build/vcpkg_installed/$TRIPLET" \
        -DCMAKE_INSTALL_RPATH_USE_LINK_PATH=ON \
        -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON \
    && cmake --build build -j$(nproc)

RUN cd build && ctest --output-on-failure
