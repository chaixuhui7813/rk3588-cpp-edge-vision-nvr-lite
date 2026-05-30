#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
if [ ! -x build/edge_vision_nvr ]; then
  bash scripts/build_board.sh
fi

SOURCE="${1:-}"
MODE="${MODE:---mock}"
FRAMES="${FRAMES:-300}"

echo "workers,total_frames,avg_fps,avg_inference_ms,avg_postprocess_ms,avg_total_ms"
for workers in 1 2 3; do
  tmp="/tmp/rk3588_edge_bench_${workers}.yaml"
  sed -E \
    -e "s/num_workers:.*/num_workers: ${workers}/" \
    -e "s/queue_size:.*/queue_size: ${QUEUE_SIZE:-2}/" \
    -e "s/save_events:.*/save_events: false/" \
    -e "s/save_preview_video:.*/save_preview_video: false/" \
    -e "s/draw_fps:.*/draw_fps: false/" \
    -e "s/benchmark:.*/benchmark: true/" \
    -e "s/benchmark_frames:.*/benchmark_frames: ${FRAMES}/" \
    config.yaml > "$tmp"
  args=(--config "$tmp" --benchmark)
  if [ "$MODE" = "--mock" ]; then args+=(--mock); fi
  if [ -n "$SOURCE" ]; then args+=(--source "$SOURCE"); fi
  out="$(./build/edge_vision_nvr "${args[@]}" 2>&1 | tee /dev/stderr | grep '\[main\] final stats:' | tail -n 1 | sed 's/.*final stats: //')"
  python3 - "$workers" "$out" <<'PY'
import json, sys
w = sys.argv[1]
stats = json.loads(sys.argv[2])
print(f"{w},{stats.get('frame_count',0)},{stats.get('fps',0):.2f},{stats.get('avg_inference_ms',0):.2f},{stats.get('avg_postprocess_ms',0):.2f},{stats.get('avg_total_ms',0):.2f}")
PY
done
