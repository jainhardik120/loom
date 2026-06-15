#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT_DIR}"

if ! command -v bear >/dev/null 2>&1; then
    echo "bear is required to generate compile_commands.json" >&2
    exit 1
fi

make clean
bear --output compile_commands.json -- make all
