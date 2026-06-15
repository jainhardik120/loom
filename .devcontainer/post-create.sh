#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT_DIR}"

git submodule update --init --recursive

if [ -d android ]; then
    printf 'sdk.dir=%s\n' "${ANDROID_SDK_ROOT:-/opt/android-sdk}" > android/local.properties
fi

make all

if command -v bear >/dev/null 2>&1; then
    ./scripts/generate-compile-commands.sh
fi
