# rk3588-cpp-edge-vision-nvr-lite

Lightweight C++ edge AI NVR for RK3588. It follows the mature deployment shape of edge NVR systems, but it is an independent small codebase: OpenCV video input, RKNN Runtime C API inference, YOLO postprocess, MJPEG web preview, event snapshots, JSONL logs, performance stats, optional MQTT placeholder, and Docker packaging.

The board-side service is C++. Python is not used as the runtime service; the only Python usage is the optional benchmark CSV formatter in `scripts/benchmark.sh`.

## Architecture

```text
OpenCV capture -> bounded frame queue -> RKNN/mock worker pool -> bounded result queue
      -> render + MJPEG latest frame
      -> async event snapshot/log queue
      -> stats JSON APIs
```

Key files:

- `src/video_pipeline.cpp`: capture, worker pool, ordered render, queue drop policy.
- `src/rknn_detector.cpp`: RKNN Runtime C API model loading and per-worker `rknn_context`.
- `src/yolo_postprocess.cpp`: letterbox, YOLO decode, coordinate restore, NMS.
- `src/mjpeg_server.cpp`: C++ HTTP/MJPEG server, no Python web framework.
- `src/event_manager.cpp`: snapshots and `data/events/events.jsonl`.

## Why C++ On RK3588

RK3588 deployments usually care about boot time, memory, latency, and predictable access to device libraries. This project keeps the hot path in C++17 and calls RKNN Runtime directly, avoiding PyTorch, TensorFlow, Ultralytics, CUDA, and Python service dependencies on the board.

## Dependencies

Required for mock mode:

- Linux Debian/Ubuntu arm64 or x86 development host
- `g++`, `cmake`
- OpenCV development package

Required for real RKNN mode:

- RK3588 / aarch64 board
- RKNN Runtime installed and working
- `rknn_api.h`
- `librknnrt.so`
- NPU device permission for `/dev/rknpu`

Check:

```bash
bash scripts/check_env.sh
```

Install base Debian dependencies when needed:

```bash
bash scripts/install_deps_debian.sh
```

RKNN Runtime is vendor/board specific; install it from your RKNN toolkit or board image, then rerun `scripts/check_env.sh`.

## Build

```bash
mkdir -p build
cd build
cmake ..
make -j$(nproc)
```

Or:

```bash
bash scripts/build_board.sh
```

If CMake cannot find RKNN Runtime, it still builds a mock-capable binary. For custom paths:

```bash
cmake -S . -B build \
  -DRKNN_INCLUDE_DIR=/path/to/rknn/include \
  -DRKNN_LIBRARY=/path/to/librknnrt.so
```

## Run Mock Mode

```bash
./build/edge_vision_nvr --config config.yaml --mock
```

Or:

```bash
bash scripts/run_mock.sh
```

Mock mode does not need a model, NPU, camera, or video file. If `video/test.mp4` is missing, it generates synthetic frames so the full pipeline still runs.

Open:

```text
http://<board-ip>:8080
```

Local machine:

```text
http://127.0.0.1:8080
```

## Run RKNN Mode

Copy your model:

```bash
cp yolov8n.rknn models/yolov8n.rknn
```

Run:

```bash
bash scripts/run_rknn.sh
```

To generate the default RK3588 model from ONNX on a Linux/x86_64 host with RKNN-Toolkit2:

```bash
bash scripts/convert_yolov8n_rknn.sh
```

This writes:

```text
models/yolov8n.rknn
```

Overrides:

```bash
./build/edge_vision_nvr --config config.yaml \
  --source /path/to/video.mp4 \
  --model models/yolov8n.rknn \
  --classes classes.txt \
  --port 8080
```

Sources can be:

- video file: `/data/test.mp4`
- USB camera: `0` or `/dev/video0`
- RTSP: `rtsp://user:pass@host/stream`

## Configuration

`config.yaml` includes:

- app port and event directory
- video source, size, reconnect
- model path, input size, class file, confidence and NMS thresholds
- runtime `num_workers`, `queue_size`, `worker0_core_mask`, `worker1_core_mask`, `worker2_core_mask`, `use_all_cores`, `loop_video`
- MQTT placeholder settings

`classes.txt` defaults to COCO 80 classes. Replace it line by line for custom models.

The default `models/yolov8n.rknn` is an INT8 RK3588 model converted from the Rockchip model-zoo YOLOv8n ONNX. It uses a 9-output YOLOv8 head, so `model.letterbox_value` is set to `0` and the C++ postprocess path decodes DFL boxes directly. `models/yolov8n_fp.rknn` is kept as a floating-point fallback.

## Multi-Thread Pipeline And RKNN Workers

The pipeline is asynchronous: capture, inference workers, render/web update, and event persistence are separate stages connected by bounded queues.

Each RKNN inference worker owns its own `rknn_context`. This matters because sharing one RKNN context across threads is unsafe and can serialize work internally. With `num_workers=1`, the system uses one context. With `num_workers=2` or `3`, it creates independent contexts and assigns worker 0/1/2 to `worker0_core_mask`, `worker1_core_mask`, and `worker2_core_mask`. Defaults are `1`, `2`, and `4`, matching RKNN core 0/1/2 masks on typical RK3588 Runtime builds.

`use_all_cores=true` uses one context with `RKNN_NPU_CORE_0_1_2` when available. This is different from multiple contexts: one context may let RKNN schedule across cores, while multiple contexts can keep independent requests in flight.

Bounded queues protect realtime behavior. If the input is faster than inference, old frames are dropped instead of growing latency forever. Tune:

- Increase `queue_size` when occasional jitter matters more than latency.
- Decrease `queue_size` when realtime preview matters most.
- Try `num_workers=1`, `2`, `3`; more workers are not always faster because preprocessing, memory bandwidth, model shape, RKNN scheduling, and thermal limits can dominate.

For RK3588 throughput mode, start with:

```yaml
runtime:
  num_workers: 3
  queue_size: 2
  use_all_cores: false
```

`web_jpeg_interval_ms` throttles MJPEG JPEG encoding for live preview. Benchmark mode skips Web JPEG encoding entirely so the reported FPS focuses on capture, RKNN inference, YOLO postprocess, and pipeline overhead.

Stats API exposes FPS, latency, per-worker FPS/inference time, queue length, dropped frames, CPU, and memory:

```bash
curl http://127.0.0.1:8080/api/stats
```

## Events

Snapshots:

```text
data/events/YYYYMMDD_HHMMSS_xxx_<frame_id>.jpg
```

Log:

```text
data/events/events.jsonl
```

Each JSONL line contains timestamp, class name, confidence, bbox, and image path.

Annotated result video:

```text
data/events/annotated_preview.mp4
```

Generate a local result video from a file:

```bash
bash scripts/render_result_video.sh data/1test.mp4
```

## Benchmark

Mock comparison:

```bash
bash scripts/benchmark.sh
```

Video file comparison:

```bash
bash scripts/benchmark.sh /path/to/video.mp4
```

RKNN comparison:

```bash
MODE=--rknn bash scripts/benchmark.sh /path/to/video.mp4
```

It compares 1, 2, and 3 workers and prints total frames, FPS, average inference, postprocess, and total latency.

On the tested RK3588 board with the INT8 Rockchip YOLOv8n RKNN model and `data/1test.mp4`, `num_workers=3`, `queue_size=2` reached about 79 FPS with NPU devfreq load sampled at 100% at 1 GHz. The older floating-point single-output model reached about 32 FPS. Larger queues did not improve FPS and increased end-to-end latency.

## Docker

Docker is optional. On RK3588, run privileged or mount devices:

```bash
docker compose up --build
```

Important mounts:

- `/dev`
- `/dev/dri`
- `/dev/rknpu`
- `./models:/app/models`
- `./data:/app/data`

The container defaults to mock mode. Change `docker-compose.yml` command for RKNN mode after mounting RKNN Runtime and model files.

## API

- `GET /`: web UI
- `GET /api/health`
- `GET /api/stats`
- `GET /api/events`
- `GET /api/video_feed`: MJPEG stream

## Troubleshooting

OpenCV not found:

```bash
sudo apt-get install libopencv-dev pkg-config
```

Or:

```bash
bash scripts/install_deps_debian.sh
```

`rknn_api.h` not found:

```bash
cmake -S . -B build -DRKNN_INCLUDE_DIR=/path/to/include
```

`librknnrt.so` not found:

```bash
export LD_LIBRARY_PATH=/path/to/rknn/lib:$LD_LIBRARY_PATH
cmake -S . -B build -DRKNN_LIBRARY=/path/to/librknnrt.so
```

Video cannot open:

- Use an absolute file path.
- Test with `--mock` first.
- For USB camera, try `--source 0`.
- Confirm OpenCV was built with FFmpeg/GStreamer if using RTSP.

RTSP disconnects:

- Keep `video.reconnect=true`.
- Reduce `queue_size` for lower latency.
- Prefer main/sub stream URLs that the board can decode comfortably.

Model output format mismatch:

- Edit the marked decode block in `src/yolo_postprocess.cpp`.
- Common YOLOv8 outputs are `[1,84,8400]` and `[1,8400,84]`.
- Custom RKNN exports may already decode boxes or may include masks; adapt there.

NPU permission denied:

```bash
ls -l /dev/rknpu
sudo usermod -aG video "$USER"
```

Then log out and back in, or run with appropriate device permissions.

## Resume Project Description

Implemented a lightweight RK3588 C++ edge AI NVR system using RKNN Runtime C API and OpenCV. Built an asynchronous multi-thread video pipeline with bounded queues, per-worker RKNN contexts for multi-core NPU inference, YOLOv8 postprocessing, MJPEG web preview, event snapshot logging, and realtime performance stats. Supported mock mode, RTSP/video/USB input, CMake deployment, and Docker packaging for board-side operation.

## Quick Start

```bash
cd rk3588-cpp-edge-vision-nvr-lite
bash scripts/check_env.sh
bash scripts/build_board.sh
bash scripts/run_mock.sh
```

With RKNN model:

```bash
bash scripts/run_rknn.sh
```
