FROM mcr.microsoft.com/devcontainers/base:ubuntu-24.04

ARG ANDROID_CMDLINE_TOOLS_URL=https://dl.google.com/android/repository/commandlinetools-linux-14742923_latest.zip
ARG ANDROID_BUILD_TOOLS_VERSION=36.0.0

ENV DEBIAN_FRONTEND=noninteractive
ENV ANDROID_HOME=/opt/android-sdk
ENV ANDROID_SDK_ROOT=/opt/android-sdk
ENV PATH="${ANDROID_HOME}/cmdline-tools/latest/bin:${ANDROID_HOME}/platform-tools:${PATH}"

RUN apt-get update && apt-get install -y --no-install-recommends \
    adb \
    bear \
    bc \
    bison \
    build-essential \
    ca-certificates \
    clang \
    clang-format \
    clangd \
    cmake \
    curl \
    dbus-user-session \
    dkms \
    dwarves \
    file \
    flex \
    gdb \
    git \
    gstreamer1.0-libav \
    gstreamer1.0-plugins-bad \
    gstreamer1.0-plugins-base \
    gstreamer1.0-plugins-good \
    gstreamer1.0-plugins-ugly \
    gstreamer1.0-tools \
    gstreamer1.0-vaapi \
    kmod \
    libayatana-appindicator3-dev \
    libdrm-dev \
    libelf-dev \
    libgstreamer-plugins-base1.0-dev \
    libgstreamer1.0-dev \
    libgtk-3-dev \
    libsystemd-dev \
    libusb-1.0-0-dev \
    linux-headers-generic \
    make \
    mesa-va-drivers \
    ninja-build \
    nodejs \
    npm \
    openjdk-21-jdk \
    pahole \
    pkg-config \
    python3 \
    python3-pip \
    sudo \
    unzip \
    vainfo \
    zip \
    && rm -rf /var/lib/apt/lists/*

RUN npm install -g pnpm \
    && mkdir -p "${ANDROID_HOME}/cmdline-tools" \
    && curl -fL "${ANDROID_CMDLINE_TOOLS_URL}" -o /tmp/android-cmdline-tools.zip \
    && unzip -q /tmp/android-cmdline-tools.zip -d /tmp/android-cmdline-tools \
    && mv /tmp/android-cmdline-tools/cmdline-tools "${ANDROID_HOME}/cmdline-tools/latest" \
    && rm -rf /tmp/android-cmdline-tools /tmp/android-cmdline-tools.zip \
    && yes | sdkmanager --licenses >/dev/null \
    && sdkmanager \
        "platform-tools" \
        "platforms;android-36" \
        "build-tools;${ANDROID_BUILD_TOOLS_VERSION}" \
        "cmdline-tools;latest"

RUN chown -R vscode:vscode "${ANDROID_HOME}"
