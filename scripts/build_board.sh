#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
mkdir -p build
cmake -S . -B build
cmake --build build -j"$(nproc)"
echo "Built: $ROOT/build/edge_vision_nvr"
