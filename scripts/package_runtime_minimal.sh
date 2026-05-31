#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

OUT_DIR="${OUT_DIR:-deploy/runtime_minimal}"
APP_NAME="${APP_NAME:-rk3588_edge_nvr_runtime_minimal}"
APP_DIR="$OUT_DIR/$APP_NAME"
SOURCE_VIDEO="${SOURCE_VIDEO:-data/vtest.avi}"
MODEL_PATH="${MODEL_PATH:-models/yolov8n.rknn}"

if [ ! -x build/edge_vision_nvr ]; then
  bash scripts/build_board.sh
fi

rm -rf "$APP_DIR"
mkdir -p "$APP_DIR"/{bin,models,web,data/events}

cp build/edge_vision_nvr "$APP_DIR/bin/"
cp config.yaml classes.txt "$APP_DIR/"
cp "$MODEL_PATH" "$APP_DIR/models/yolov8n.rknn"
cp models/yolov8n.rknn.info.txt "$APP_DIR/models/" 2>/dev/null || true
cp web/* "$APP_DIR/web/"
if [ -f "$SOURCE_VIDEO" ]; then
  cp "$SOURCE_VIDEO" "$APP_DIR/data/"
fi
touch "$APP_DIR/data/events/.gitkeep"

cat > "$APP_DIR/RUN.md" <<'EOF'
# RK3588 Edge Vision NVR Lite Runtime

This is the minimal board runtime package.

Run:

```bash
./bin/edge_vision_nvr --config config.yaml --source data/vtest.avi --port 8080
```

Open:

```text
http://BOARD_IP:8080
```
EOF

sed -i -E \
  -e 's/save_events:.*/save_events: false/' \
  -e 's/save_preview_video:.*/save_preview_video: false/' \
  -e 's#source:.*#source: data/vtest.avi#' \
  -e 's/num_workers:.*/num_workers: 3/' \
  -e 's/queue_size:.*/queue_size: 2/' \
  -e 's/letterbox_value:.*/letterbox_value: 0/' \
  "$APP_DIR/config.yaml"

TAR="$OUT_DIR/${APP_NAME}_$(date +%Y%m%d_%H%M%S).tar.gz"
tar -C "$OUT_DIR" -czf "$TAR" "$APP_NAME"

echo "[runtime] directory: $APP_DIR"
du -sh "$APP_DIR"
echo "[runtime] archive: $TAR"
ls -lh "$TAR"
