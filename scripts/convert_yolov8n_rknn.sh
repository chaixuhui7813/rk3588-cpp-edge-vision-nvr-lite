#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

if [ ! -d .venv-rknn ]; then
  python3 -m venv .venv-rknn
fi

. .venv-rknn/bin/activate
python -m pip install --upgrade pip
python -m pip install \
  rknn-toolkit2==2.3.2 \
  onnx==1.16.1 \
  numpy==1.26.4 \
  protobuf==4.25.4 \
  pillow \
  opencv-python

if [ ! -f models/yolov8n.onnx ]; then
  curl -L --fail -o models/yolov8n.onnx \
    https://huggingface.co/webml/yolov8n/resolve/main/onnx/yolov8n.onnx
fi

python tools/rknn/convert_yolov8n_to_rknn.py "$@"
