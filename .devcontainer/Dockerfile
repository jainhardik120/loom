FROM ubuntu:26.04

ARG ANDROID_CMDLINE_TOOLS_URL=https://dl.google.com/android/repository/commandlinetools-linux-14742923_latest.zip
ARG ANDROID_BUILD_TOOLS_VERSION=36.0.0

ENV DEBIAN_FRONTEND=noninteractive
ENV ANDROID_HOME=/opt/android-sdk
ENV ANDROID_SDK_ROOT=/opt/android-sdk
ENV PATH="${ANDROID_HOME}/cmdline-tools/latest/bin:${ANDROID_HOME}/platform-tools:${PATH}"

RUN apt-get update && apt-get install -y --no-install-recommends \
    bear \
    bc \
    bison \
    build-essential \
    ca-certificates \
    clang \
    clang-format \
    clangd \
    curl \
    dbus-user-session \
    dkms \
    dwarves \
    file \
    flex \
    gdb \
    git \
    kmod \
    libayatana-appindicator3-dev \
    libdrm-dev \
    libelf-dev \
    libgtk-3-dev \
    libsystemd-dev \
    libusb-1.0-0-dev \
    linux-headers-generic \
    make \
    openjdk-21-jdk \
    pahole \
    pkg-config \
    python3 \
    sudo \
    unzip \
    zip \
    && rm -rf /var/lib/apt/lists/*

RUN useradd -ms /bin/bash vscode \
    && usermod -aG sudo vscode \
    && echo "vscode ALL=(ALL) NOPASSWD:ALL" >/etc/sudoers.d/vscode \
    && chmod 0440 /etc/sudoers.d/vscode

RUN mkdir -p "${ANDROID_HOME}/cmdline-tools" \
    && curl -fL "${ANDROID_CMDLINE_TOOLS_URL}" -o /tmp/android-cmdline-tools.zip \
    && unzip -q /tmp/android-cmdline-tools.zip -d /tmp/android-cmdline-tools \
    && mv /tmp/android-cmdline-tools/cmdline-tools "${ANDROID_HOME}/cmdline-tools/latest" \
    && rm -rf /tmp/android-cmdline-tools /tmp/android-cmdline-tools.zip \
    && yes | sdkmanager --licenses >/dev/null \
    && sdkmanager \
        "platform-tools" \
        "platforms;android-36" \
        "platforms;android-36.1" \
        "build-tools;${ANDROID_BUILD_TOOLS_VERSION}"

RUN chown -R vscode:vscode "${ANDROID_HOME}"
