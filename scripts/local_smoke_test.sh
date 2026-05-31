#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

SOURCE="${1:-data/1test.mp4}"
PORT="${PORT:-8091}"
LOG="/tmp/edge_nvr_smoke.log"
MJPEG="/tmp/edge_nvr_smoke_mjpeg.bin"

cmake --build build -j"$(nproc)"

rm -f "$LOG" "$MJPEG"
./build/edge_vision_nvr --config config.yaml --mock --source "$SOURCE" --port "$PORT" > "$LOG" 2>&1 &
PID=$!

cleanup() {
  kill "$PID" 2>/dev/null || true
  wait "$PID" 2>/dev/null || true
}
trap cleanup EXIT

sleep 3

echo "--- health ---"
curl -fsS "http://127.0.0.1:${PORT}/api/health"
echo

echo "--- stats frame_count ---"
curl -fsS "http://127.0.0.1:${PORT}/api/stats" | python3 -c 'import sys,json; print(json.load(sys.stdin)["frame_count"])'

echo "--- events count ---"
curl -fsS "http://127.0.0.1:${PORT}/api/events" | python3 -c 'import sys,json; print(len(json.load(sys.stdin)))'

echo "--- mjpeg bytes in 1s ---"
curl -fsS --max-time 1 "http://127.0.0.1:${PORT}/api/video_feed" -o "$MJPEG" || true
wc -c "$MJPEG"

echo "--- annotated preview video ---"
ls -lh data/events/annotated_preview.mp4 2>/dev/null || true

echo "--- read_failed_count ---"
grep -c "read failed" "$LOG" || true

echo "--- log tail ---"
tail -n 12 "$LOG"
