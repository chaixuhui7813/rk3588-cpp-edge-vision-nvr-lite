#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

STAMP="$(date +%Y%m%d_%H%M%S)"
BASE="$ROOT/deploy/rk3588_edge_nvr_minimal_$STAMP"
APP="$BASE/rk3588-cpp-edge-vision-nvr-lite"

mkdir -p "$APP"/{include,src,scripts,models,data/events,third_party/httplib,web}

cp CMakeLists.txt config.yaml classes.txt "$APP/"
cp include/*.hpp "$APP/include/"
cp src/*.cpp "$APP/src/"
cp third_party/httplib/httplib.h "$APP/third_party/httplib/"
cp web/* "$APP/web/"

cp scripts/check_env.sh "$APP/scripts/"
cp scripts/build_board.sh "$APP/scripts/"
cp scripts/run_mock.sh "$APP/scripts/"
cp scripts/run_rknn.sh "$APP/scripts/"
cp scripts/local_smoke_test.sh "$APP/scripts/"
cp scripts/render_result_video.sh "$APP/scripts/"

cp models/yolov8n.rknn "$APP/models/"
cp models/yolov8n.rknn.info.txt "$APP/models/" 2>/dev/null || true
cp models/README.md "$APP/models/"
cp data/1test.mp4 "$APP/data/"
touch "$APP/data/events/.gitkeep"

cat > "$APP/BOARD_RUN.md" <<'EOF'
# RK3588 Minimal Board Run

This directory is intentionally self-contained and small. It does not modify
system packages and does not delete anything on the board.

## 1. Check only

```bash
bash scripts/check_env.sh
```

Do not install dependencies on a borrowed board unless the owner approves.

## 2. Build if CMake/OpenCV/RKNN headers/libs already exist

```bash
bash scripts/build_board.sh
```

If RKNN Runtime is installed in a non-standard path:

```bash
cmake -S . -B build \
  -DRKNN_INCLUDE_DIR=/path/to/rknn/include \
  -DRKNN_LIBRARY=/path/to/librknnrt.so
cmake --build build -j$(nproc)
```

## 3. Run mock first

```bash
./build/edge_vision_nvr --config config.yaml --mock --source data/1test.mp4 --port 8080
```

Open:

```text
http://BOARD_IP:8080
```

## 4. Run RKNN

```bash
./build/edge_vision_nvr --config config.yaml --source data/1test.mp4 --port 8080
```

Outputs:

```text
data/events/annotated_preview.mp4
data/events/events.jsonl
data/events/*.jpg
```
EOF

chmod +x "$APP/scripts/"*.sh

TAR="$BASE.tar.gz"
tar -C "$BASE" -czf "$TAR" rk3588-cpp-edge-vision-nvr-lite

echo "Minimal board bundle directory:"
du -sh "$APP"
echo "$APP"
echo
echo "Archive:"
ls -lh "$TAR"
echo "$TAR"
