# rk3588-cpp-edge-vision-nvr-lite

[English README](README.md)

这是一个面向 RK3588 的轻量级 C++ 边缘 AI 视频 NVR 项目。项目参考成熟边缘 NVR 的部署形态，但不依赖 Frigate，也不复制 Frigate 源码。核心程序使用 C++17 实现，通过 OpenCV 读取视频文件、USB 摄像头或 RTSP 流，通过 RKNN Runtime C API 调用 RK3588 NPU 做 YOLOv8 目标检测，并提供 MJPEG Web 实时预览、事件记录、性能统计和可选 MQTT 占位接口。

板端主服务不是 Python。Python 只用于可选的主机侧工具，例如 RKNN 模型转换脚本，不承担板端推理服务。

## 项目亮点

- RK3588 / aarch64 / Linux 板端部署。
- C++17 主服务，无 PyTorch、TensorFlow、Ultralytics 等板端依赖。
- RKNN Runtime C API 推理。
- 默认模型为 RK3588 INT8 YOLOv8n RKNN。
- 支持 Rockchip model-zoo YOLOv8 9 输出 DFL 后处理。
- 支持常见单输出 YOLOv8 `[1,84,8400]` / `[1,8400,84]` 兜底后处理。
- 多线程异步流水线：采集、推理 worker、渲染/Web、事件写入解耦。
- 每个 RKNN 推理 worker 独立持有一个 `rknn_context`。
- 有界队列，队列满时丢弃旧帧，优先保证实时性。
- Web 实时预览、事件 JSON、系统性能统计接口。
- 支持 mock 模式，没有 NPU/模型/摄像头也能跑通流程。
- 支持 CMake 构建和 Docker 可选部署。

## 架构

```text
OpenCV 采集
  -> 有界帧队列
  -> RKNN/mock 推理 worker 池
  -> 有界结果队列
  -> 绘制检测框 + MJPEG 最新帧
  -> 异步事件截图/JSONL 日志
  -> /api/stats 性能统计
```

关键文件：

- `src/video_pipeline.cpp`：视频采集、推理线程池、按帧号渲染、队列丢帧策略。
- `src/rknn_detector.cpp`：RKNN 模型加载、输入输出查询、每 worker 独立 `rknn_context`。
- `src/yolo_postprocess.cpp`：letterbox、YOLOv8 DFL 解码、坐标还原、NMS。
- `src/mjpeg_server.cpp`：C++ HTTP/MJPEG 服务，不使用 Python Web 框架。
- `src/event_manager.cpp`：事件截图和 `data/events/events.jsonl`。
- `src/system_stats.cpp`：FPS、延迟、worker、队列、CPU、内存统计。

## 为什么使用 C++

RK3588 板端部署通常更关注启动速度、内存占用、推理延迟和对 NPU 运行库的稳定调用。这个项目把热路径保留在 C++ 中，直接调用 RKNN Runtime，避免把 Python 训练框架带到板端环境中。

## 依赖

mock 模式需要：

- Linux Debian/Ubuntu arm64 或 x86 开发环境
- `g++`
- `cmake`
- OpenCV 开发包

真实 RKNN 模式需要：

- RK3588 / aarch64 板子
- 已安装并可用的 RKNN Runtime
- `rknn_api.h`
- `librknnrt.so`
- NPU 设备权限，例如 `/dev/rknpu` 或部分系统上的 `/dev/dri/renderD*`

检查环境：

```bash
bash scripts/check_env.sh
```

安装基础 Debian 依赖：

```bash
bash scripts/install_deps_debian.sh
```

RKNN Runtime 与板卡镜像相关，请使用板卡厂商或 RKNN Toolkit 提供的运行库。

## 编译

```bash
mkdir -p build
cd build
cmake ..
make -j$(nproc)
```

或直接运行：

```bash
bash scripts/build_board.sh
```

如果 CMake 找不到 RKNN Runtime，仍然会构建可运行 mock 模式的程序。自定义 RKNN 路径：

```bash
cmake -S . -B build \
  -DRKNN_INCLUDE_DIR=/path/to/rknn/include \
  -DRKNN_LIBRARY=/path/to/librknnrt.so
```

## 运行 mock 模式

```bash
./build/edge_vision_nvr --config config.yaml --mock
```

或：

```bash
bash scripts/run_mock.sh
```

mock 模式不需要模型、NPU、摄像头或视频文件。如果默认视频源不存在，会生成合成帧，仍然可以跑通视频读取、检测框绘制、Web 预览、事件和统计流程。

浏览器打开：

```text
http://<board-ip>:8080
```

本机测试：

```text
http://127.0.0.1:8080
```

## 运行 RKNN 模式

默认模型路径：

```text
models/yolov8n.rknn
```

运行：

```bash
bash scripts/run_rknn.sh
```

指定视频、模型、类别文件和端口：

```bash
./build/edge_vision_nvr --config config.yaml \
  --source /path/to/video.mp4 \
  --model models/yolov8n.rknn \
  --classes classes.txt \
  --port 8080
```

支持的视频源：

- 视频文件：`data/vtest.avi`、`/data/test.mp4`
- USB 摄像头：`0` 或 `/dev/video0`
- RTSP：`rtsp://user:pass@host/stream`

没有摄像头也没关系，推荐先用视频文件验证。

## 默认模型

默认 `models/yolov8n.rknn` 是 RK3588 INT8 YOLOv8n 模型，来自 Rockchip model-zoo YOLOv8 ONNX 的量化转换结果。它使用 9 输出 YOLOv8 head：

```text
stride 8:  box DFL + class + score
stride 16: box DFL + class + score
stride 32: box DFL + class + score
```

因此默认配置中：

```yaml
model:
  letterbox_value: 0
```

C++ 后处理会直接解码 DFL box，不依赖 Python、Torch 或 NumPy。

如果你替换为自定义 YOLOv8 RKNN 模型，例如安全帽、反光背心、烟火、钢材缺陷检测，请同步修改：

- `models/yolov8n.rknn`
- `classes.txt`
- `config.yaml` 中的输入尺寸、阈值和 `letterbox_value`
- 如输出格式不同，修改 `src/yolo_postprocess.cpp`

## 配置文件

`config.yaml` 包含：

- `app`：Web 端口、事件目录、是否保存截图/结果视频。
- `video`：视频源、目标宽高、断流重连。
- `model`：模型路径、输入尺寸、类别文件、置信度阈值、NMS 阈值。
- `runtime`：RKNN 开关、NPU core mask、worker 数、队列长度、benchmark。
- `mqtt`：可选 MQTT 占位配置。

推荐 RK3588 单路吞吐配置：

```yaml
runtime:
  num_workers: 3
  queue_size: 2
  use_all_cores: false
```

## 多线程流水线与多 RKNN context

本项目不是单线程 demo。采集、推理、渲染、事件写入是解耦的异步阶段。

每个 RKNN worker 拥有独立 `rknn_context`。这是因为多线程共享同一个 RKNN context 不安全，也可能在 Runtime 内部串行化。配置含义：

- `num_workers=1`：一个 RKNN context，稳定模式。
- `num_workers=2`：两个 RKNN context，并发推理。
- `num_workers=3`：三个 RKNN context，尝试压满 RK3588 三核 NPU。

默认 core mask：

```yaml
worker0_core_mask: 1
worker1_core_mask: 2
worker2_core_mask: 4
```

`use_all_cores=true` 表示单 context 使用 `RKNN_NPU_CORE_0_1_2`。这和多 context worker 不一样：单 context 让 RKNN 在内部调度，多 context 则让多个独立请求同时在飞。

## 为什么需要有界队列

实时视频系统不能让队列无限堆积。输入帧速度超过推理速度时，如果不断排队，Web 预览会越来越“滞后”。本项目的有界队列在满时丢弃旧帧，优先显示最新结果。

调参建议：

- `queue_size=1`：最低延迟，但吞吐可能略低。
- `queue_size=2`：本项目默认推荐，吞吐与延迟比较均衡。
- 更大的 `queue_size`：抗抖动更强，但端到端延迟会增加。

## Web 与 API

Web 页面：

```text
GET /
```

接口：

```text
GET /api/health
GET /api/stats
GET /api/events
GET /api/video_feed
```

查看统计：

```bash
curl http://127.0.0.1:8080/api/stats
```

统计包含：

- 总 FPS
- 平均 NPU 推理耗时
- 平均后处理耗时
- 平均端到端延迟
- 每个 worker 的 FPS 和平均推理耗时
- 队列长度和丢帧数量
- CPU 占用
- 内存占用

`web_jpeg_interval_ms` 用于限制 Web MJPEG JPEG 编码频率。benchmark 模式会跳过 Web JPEG 编码，使性能数据更接近纯推理流水线。

## 事件

截图路径：

```text
data/events/YYYYMMDD_HHMMSS_xxx_<frame_id>.jpg
```

日志：

```text
data/events/events.jsonl
```

每条事件包含：

- timestamp
- class_name
- confidence
- bbox
- image_path

生成标注结果视频：

```bash
bash scripts/render_result_video.sh data/vtest.avi
```

## Benchmark

mock 对比：

```bash
bash scripts/benchmark.sh
```

视频文件对比：

```bash
bash scripts/benchmark.sh data/vtest.avi
```

RKNN 对比：

```bash
MODE=--rknn bash scripts/benchmark.sh data/vtest.avi
```

脚本会对比 1、2、3 个 worker，并输出：

- 总帧数
- 平均 FPS
- 平均推理耗时
- 平均后处理耗时
- 平均端到端延迟

已验证板端数据：

```text
平台: RK3588
模型: INT8 Rockchip YOLOv8n RKNN
配置: num_workers=3, queue_size=2
NPU: 100%@1GHz
吞吐: 约 79 FPS
平均 NPU 推理: 约 24 ms
平均后处理: 约 1-2 ms
```

旧 FP 单输出模型三 worker 约 32 FPS。正确 INT8 模型带来了主要性能提升。

## Docker

Docker 是可选项，不是必须部署方式。

```bash
docker compose up --build
```

RK3588 上运行容器时通常需要挂载：

- `/dev`
- `/dev/dri`
- `/dev/rknpu`
- `./models:/app/models`
- `./data:/app/data`

默认容器命令偏向 mock 模式。真实 RKNN 模式需要确认容器内能访问 RKNN Runtime、NPU 设备和模型文件。

## 常见问题

### 找不到 OpenCV

```bash
sudo apt-get install libopencv-dev pkg-config
```

或：

```bash
bash scripts/install_deps_debian.sh
```

### 找不到 `rknn_api.h`

```bash
cmake -S . -B build -DRKNN_INCLUDE_DIR=/path/to/include
```

### 找不到 `librknnrt.so`

```bash
export LD_LIBRARY_PATH=/path/to/rknn/lib:$LD_LIBRARY_PATH
cmake -S . -B build -DRKNN_LIBRARY=/path/to/librknnrt.so
```

### 视频打不开

- 使用绝对路径测试。
- 先用 `--mock` 验证完整流程。
- USB 摄像头可尝试 `--source 0`。
- RTSP 需要确认 OpenCV 编译时启用了 FFmpeg 或 GStreamer。

### RTSP 断流

- 保持 `video.reconnect=true`。
- 降低 `queue_size` 减少延迟。
- 优先使用板子能稳定解码的主码流或子码流。

### 模型输出格式不匹配

修改 `src/yolo_postprocess.cpp`。当前支持：

- Rockchip YOLOv8 9 输出 DFL head。
- 常见 YOLOv8 单输出 `[1,84,8400]`。
- 常见 YOLOv8 单输出 `[1,8400,84]`。

自定义分割、关键点或已解码输出需要单独适配。

### NPU 权限不足

```bash
ls -l /dev/rknpu
sudo usermod -aG video "$USER"
```

然后重新登录，或使用合适的设备权限运行。

部分 RK3588 镜像的 NPU 设备会表现为 `/dev/dri/renderD*`。

## 最小运行包

最终板端最小运行目录只需要：

```text
bin/edge_vision_nvr
config.yaml
classes.txt
models/yolov8n.rknn
web/
data/vtest.avi 或你的测试视频
data/events/
```

不需要源码、CMake、build 中间文件或 Python 环境。

## 简历项目描述

实现了一个面向 RK3588 的轻量级 C++ 边缘 AI 视频 NVR 系统。项目基于 RKNN Runtime C API 和 OpenCV，构建了多线程异步视频流水线，通过有界队列连接采集、推理、渲染、Web 预览和事件记录阶段。每个 RKNN worker 独立持有 `rknn_context`，支持多 context 并发推理以提升 RK3588 三核 NPU 利用率。实现 YOLOv8 INT8 RKNN 推理、Rockchip 9 输出 DFL 后处理、MJPEG 实时预览、事件 JSONL 日志、性能统计 API、mock 模式和 CMake/Docker 部署。板端测试中，INT8 YOLOv8n 在三 worker 配置下达到约 79 FPS，NPU 占用 100%@1GHz。

## 快速开始

```bash
cd rk3588-cpp-edge-vision-nvr-lite
bash scripts/check_env.sh
bash scripts/build_board.sh
bash scripts/run_mock.sh
```

真实 RKNN 模式：

```bash
bash scripts/run_rknn.sh
```
