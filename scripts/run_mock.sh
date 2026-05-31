#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
if [ ! -x build/edge_vision_nvr ]; then
  bash scripts/build_board.sh
fi
./build/edge_vision_nvr --config config.yaml --mock "$@"
