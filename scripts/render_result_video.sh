#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

SOURCE="${1:-data/1test.mp4}"
OUT="${OUT:-data/events/annotated_preview.mp4}"
FRAMES="${FRAMES:-300}"
TMP="/tmp/edge_nvr_render_video.yaml"

sed -E \
  -e "s#preview_video_path:.*#preview_video_path: ${OUT}#" \
  -e "s/benchmark:.*/benchmark: true/" \
  -e "s/benchmark_frames:.*/benchmark_frames: ${FRAMES}/" \
  -e "s/loop_video:.*/loop_video: true/" \
  config.yaml > "$TMP"

cmake --build build -j"$(nproc)"
rm -f "$OUT"
./build/edge_vision_nvr --config "$TMP" --mock --source "$SOURCE" --port 8092

echo "Annotated result video:"
ls -lh "$OUT"
