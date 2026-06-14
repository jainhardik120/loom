#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."
make
LD_LIBRARY_PATH=../third_party/evdi/library sudo -E ./build/loomd "$@"
