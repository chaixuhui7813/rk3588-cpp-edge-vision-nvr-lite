# RK3588 板端验证手册

本文档用于把项目从“能编译”验证到“可复现、可交付”。所有命令默认在项目根目录执行。

## 1. 验证目标

需要确认以下内容：

- 板端环境不被破坏。
- OpenCV、CMake、g++、RKNN Runtime 可用。
- mock 模式可运行。
- RKNN INT8 模型可加载并使用 NPU 推理。
- Web 页面可以实时预览。
- `/api/stats` 可以返回 FPS、延迟、队列、worker、CPU、内存等指标。
- benchmark 可以自动对比 1/2/3 workers。
- 最小 runtime 包可以独立运行。

## 2. 环境检查

```bash
bash scripts/check_env.sh
```

重点看：

- `uname -m` 是否为 `aarch64`
- OpenCV 是否可用
- `rknn_api.h` 是否存在
- `librknnrt.so` 是否存在
- `/dev/rknpu` 或 `/dev/dri/renderD*` 是否存在
- 磁盘和内存是否充足

如果板子不是自己的，不要主动安装系统依赖。只记录缺失项，和板子 owner 确认后再处理。

## 3. 编译

```bash
bash scripts/build_board.sh
```

成功后应生成：

```text
build/edge_vision_nvr
```

如果找不到 RKNN Runtime，可以先跑 mock 模式验证主流程；真实 RKNN 模式需要补齐 `rknn_api.h` 和 `librknnrt.so` 后重新 CMake。

## 4. mock 模式验证

```bash
./build/edge_vision_nvr --config config.yaml --mock --source data/vtest.avi --port 8080
```

浏览器打开：

```text
http://BOARD_IP:8080
```

验证点：

- 页面能打开。
- MJPEG 画面持续刷新。
- `/api/health` 返回 `detector=mock`。
- `/api/stats` 中 `frame_count` 增长。

## 5. RKNN 模式验证

```bash
./build/edge_vision_nvr --config config.yaml --source data/vtest.avi --port 8080
```

验证点：

- 日志中出现 `worker 0/1/2 initialized`。
- `outputs=9` 表示默认 Rockchip YOLOv8 INT8 模型输出格式正确。
- Web 画面有检测框。
- `/api/health` 返回 `detector=rknn`。

接口检查：

```bash
curl http://127.0.0.1:8080/api/health
curl http://127.0.0.1:8080/api/stats
```

## 6. 自动 benchmark

推荐在没有浏览器访问、关闭事件截图和结果视频的情况下跑 benchmark：

```bash
MODE=--rknn FRAMES=300 QUEUE_SIZE=2 bash scripts/benchmark_workers.sh data/vtest.avi
```

输出目录示例：

```text
benchmark_results/20260531_153000/
  benchmark_summary.csv
  benchmark_summary.json
  benchmark_workers_1.log
  benchmark_workers_2.log
  benchmark_workers_3.log
  metrics_workers_1.csv
  metrics_workers_2.csv
  metrics_workers_3.csv
```

`benchmark_summary.csv` 用于快速对比：

```text
workers,total_frames,avg_fps,avg_inference_ms,avg_postprocess_ms,avg_total_ms,...
```

`benchmark_summary.json` 保留完整 `/api/stats` 风格数据，适合归档。

## 7. NPU / CPU / 内存采集

单独采集：

```bash
INTERVAL=1 PID_FILTER=edge_vision_nvr bash scripts/collect_system_metrics.sh benchmark_results/manual_metrics.csv
```

字段说明：

- `cpu_percent`：系统 CPU 占用。
- `mem_used_mb`：系统已用内存。
- `mem_available_mb`：系统可用内存。
- `process_rss_mb`：`edge_vision_nvr` 进程 RSS。
- `npu_load_percent`：NPU devfreq load。
- `npu_cur_freq_hz`：NPU 当前频率。
- `npu_governor`：NPU 调频策略。

如果某些板卡没有暴露 `/sys/class/devfreq/*npu*/load`，脚本会返回默认 0，不影响主流程。

## 8. 最小 runtime 打包

在板端编译完成后执行：

```bash
bash scripts/package_runtime_minimal.sh
```

默认输出：

```text
deploy/runtime_minimal/rk3588_edge_nvr_runtime_minimal/
deploy/runtime_minimal/rk3588_edge_nvr_runtime_minimal_YYYYMMDD_HHMMSS.tar.gz
```

最小 runtime 包只包含：

```text
bin/edge_vision_nvr
config.yaml
classes.txt
models/yolov8n.rknn
web/
data/vtest.avi
data/events/.gitkeep
RUN.md
```

不包含源码、CMake 中间文件、Python 虚拟环境、benchmark 结果和事件截图。

运行 runtime 包：

```bash
cd deploy/runtime_minimal/rk3588_edge_nvr_runtime_minimal
./bin/edge_vision_nvr --config config.yaml --source data/vtest.avi --port 8080
```

## 9. 建议验收标准

单路 `data/vtest.avi` 或同类视频：

- 程序稳定运行。
- Web 页面可访问。
- `/api/stats` 正常返回。
- 1/2/3 workers benchmark 数据完整落盘。
- NPU load 可采集时应能看到推理期间负载上升。
- 3 workers 相比 1 worker 有明显吞吐提升。
- 最小 runtime 包可独立启动。

## 10. 注意事项

- 借来的板子不要安装大依赖，不要覆盖系统 RKNN Runtime。
- benchmark 时关闭事件截图和结果视频，避免 I/O 污染性能数据。
- 视频文件 benchmark 是吞吐测试，不等同于真实摄像头帧率。
- 大队列不一定提高 FPS，反而可能增加端到端延迟。
- 更换自定义模型后必须重新确认输出格式和 `letterbox_value`。
