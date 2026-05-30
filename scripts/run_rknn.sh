#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
if [ ! -f models/yolov8n.rknn ]; then
  echo "Missing models/yolov8n.rknn"
  echo "Copy your RKNN model into models/ or pass --model /path/to/model.rknn"
  exit 1
fi
if [ ! -x build/edge_vision_nvr ]; then
  bash scripts/build_board.sh
fi
./build/edge_vision_nvr --config config.yaml "$@"
