# 交付检查清单

## 代码与构建

- [ ] `bash scripts/check_env.sh` 能输出环境信息。
- [ ] `bash scripts/build_board.sh` 构建成功。
- [ ] `./build/edge_vision_nvr --help` 能输出启动参数。
- [ ] mock 模式可运行。
- [ ] RKNN 模式可加载 `models/yolov8n.rknn`。

## Web 与接口

- [ ] `http://BOARD_IP:8080` 页面可访问。
- [ ] `GET /api/health` 返回 `status=ok`。
- [ ] `GET /api/stats` 返回 FPS、延迟、worker、队列、CPU、内存。
- [ ] `GET /api/video_feed` 浏览器可显示 MJPEG。

## Benchmark

- [ ] 执行：

```bash
MODE=--rknn FRAMES=300 QUEUE_SIZE=2 bash scripts/benchmark_workers.sh data/vtest.avi
```

- [ ] 生成 `benchmark_summary.csv`。
- [ ] 生成 `benchmark_summary.json`。
- [ ] 生成每个 worker 的 log。
- [ ] 生成每个 worker 的 metrics CSV。
- [ ] 1/2/3 workers 数据符合预期趋势。

## Runtime 包

- [ ] 执行：

```bash
bash scripts/package_runtime_minimal.sh
```

- [ ] runtime 包中只包含运行必需文件。
- [ ] runtime 包不包含源码、CMake 中间文件、Python 虚拟环境。
- [ ] runtime 包可独立启动。

## 板端环境

- [ ] 不覆盖系统 RKNN Runtime。
- [ ] 不安装非必要依赖。
- [ ] 不留下临时 benchmark 大文件。
- [ ] 事件截图默认关闭，避免占满磁盘。

## 文档

- [ ] `README.md` 英文入口完整。
- [ ] `README.zh-CN.md` 中文入口完整。
- [ ] `docs/BOARD_VALIDATION.zh-CN.md` 可指导板端复现。
- [ ] `docs/INTERVIEW_GUIDE.zh-CN.md` 可用于面试讲解。
