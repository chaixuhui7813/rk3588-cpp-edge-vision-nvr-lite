#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

SOURCE="${1:-data/vtest.avi}"
MODE="${MODE:---rknn}"
FRAMES="${FRAMES:-300}"
QUEUE_SIZE="${QUEUE_SIZE:-2}"
OUT_DIR="${OUT_DIR:-benchmark_results/$(date +%Y%m%d_%H%M%S)}"
COLLECT_METRICS="${COLLECT_METRICS:-1}"
PORT_BASE="${PORT_BASE:-18080}"

mkdir -p "$OUT_DIR"

if [ ! -x build/edge_vision_nvr ]; then
  bash scripts/build_board.sh
fi

CSV="$OUT_DIR/benchmark_summary.csv"
JSON="$OUT_DIR/benchmark_summary.json"
echo "workers,total_frames,avg_fps,avg_inference_ms,avg_postprocess_ms,avg_total_ms,avg_capture_ms,avg_preprocess_ms,avg_render_ms,event_count,dropped_frames,cpu_percent,memory_mb" > "$CSV"
printf '[\n' > "$JSON"

first_json=1
for workers in 1 2 3; do
  port=$((PORT_BASE + workers))
  tmp="$OUT_DIR/benchmark_workers_${workers}.yaml"
  log="$OUT_DIR/benchmark_workers_${workers}.log"
  metrics="$OUT_DIR/metrics_workers_${workers}.csv"

  sed -E \
    -e "s/num_workers:.*/num_workers: ${workers}/" \
    -e "s/queue_size:.*/queue_size: ${QUEUE_SIZE}/" \
    -e "s/save_events:.*/save_events: false/" \
    -e "s/save_preview_video:.*/save_preview_video: false/" \
    -e "s/draw_fps:.*/draw_fps: false/" \
    -e "s/benchmark:.*/benchmark: true/" \
    -e "s/benchmark_frames:.*/benchmark_frames: ${FRAMES}/" \
    -e "s/web_port:.*/web_port: ${port}/" \
    config.yaml > "$tmp"

  sampler_pid=""
  if [ "$COLLECT_METRICS" = "1" ]; then
    INTERVAL=1 PID_FILTER=edge_vision_nvr bash scripts/collect_system_metrics.sh "$metrics" &
    sampler_pid="$!"
  fi

  args=(--config "$tmp" --benchmark --source "$SOURCE" --port "$port")
  if [ "$MODE" = "--mock" ]; then
    args+=(--mock)
  fi

  set +e
  ./build/edge_vision_nvr "${args[@]}" 2>&1 | tee "$log"
  rc=${PIPESTATUS[0]}
  set -e

  if [ -n "$sampler_pid" ]; then
    kill "$sampler_pid" 2>/dev/null || true
    wait "$sampler_pid" 2>/dev/null || true
  fi

  if [ "$rc" -ne 0 ]; then
    echo "[benchmark] workers=${workers} failed, rc=${rc}. See $log" >&2
    exit "$rc"
  fi

  stats="$(grep '\[main\] final stats:' "$log" | tail -n 1 | sed 's/.*final stats: //')"
  if [ -z "$stats" ]; then
    echo "[benchmark] missing final stats for workers=${workers}. See $log" >&2
    exit 1
  fi

  python3 - "$workers" "$stats" "$CSV" "$JSON" "$first_json" <<'PY'
import csv
import json
import sys
from pathlib import Path

workers = int(sys.argv[1])
stats = json.loads(sys.argv[2])
csv_path = Path(sys.argv[3])
json_path = Path(sys.argv[4])
first_json = sys.argv[5] == "1"

row = {
    "workers": workers,
    "total_frames": stats.get("frame_count", 0),
    "avg_fps": stats.get("fps", 0.0),
    "avg_inference_ms": stats.get("avg_inference_ms", 0.0),
    "avg_postprocess_ms": stats.get("avg_postprocess_ms", 0.0),
    "avg_total_ms": stats.get("avg_total_ms", 0.0),
    "avg_capture_ms": stats.get("avg_capture_ms", 0.0),
    "avg_preprocess_ms": stats.get("avg_preprocess_ms", 0.0),
    "avg_render_ms": stats.get("avg_render_ms", 0.0),
    "event_count": stats.get("event_count", 0),
    "dropped_frames": stats.get("dropped_frames", 0),
    "cpu_percent": stats.get("cpu_percent", 0.0),
    "memory_mb": stats.get("memory_mb", 0.0),
}

with csv_path.open("a", newline="", encoding="utf-8") as f:
    writer = csv.DictWriter(f, fieldnames=list(row.keys()))
    writer.writerow(row)

entry = {"workers": workers, "stats": stats}
with json_path.open("a", encoding="utf-8") as f:
    if not first_json:
        f.write(",\n")
    json.dump(entry, f, ensure_ascii=False, indent=2)
PY
  first_json=0
done

printf '\n]\n' >> "$JSON"

echo
echo "[benchmark] summary CSV: $CSV"
echo "[benchmark] summary JSON: $JSON"
cat "$CSV"
