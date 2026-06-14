#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/../.."
make loomd
if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
  ./build/loomd "$@"
  exit 0
fi
LD_LIBRARY_PATH=third_party/evdi/library sudo -E ./build/loomd "$@"
