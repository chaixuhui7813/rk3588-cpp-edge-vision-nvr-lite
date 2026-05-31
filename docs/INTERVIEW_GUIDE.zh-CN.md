# 中文面试讲解稿

本文档用于面试时解释项目背景、技术难点、解决方案和可量化结果。建议按自己的表达习惯改写，不要整段背诵。

## 1. 一句话介绍项目

我做了一个面向 RK3588 的轻量级 C++ 边缘 AI 视频 NVR 系统。它可以读取视频文件、USB 摄像头或 RTSP 流，通过 RKNN Runtime 调用 RK3588 NPU 跑 YOLOv8 INT8 模型，并提供 Web 实时预览、事件记录和性能统计。

## 2. 为什么做这个项目

我想验证一个完整的边缘 AI 部署链路，而不是只做 Python demo。重点是把模型真正部署到 RK3588 板端，用 C++ 调 RKNN Runtime，解决视频流、NPU 推理、多线程流水线、Web 预览和性能观测这些工程问题。

## 3. 技术架构怎么讲

可以这样讲：

```text
OpenCV 负责视频采集
有界队列连接各个阶段
多个 RKNN inference worker 并发推理
每个 worker 独立持有 rknn_context
后处理线程完成 YOLO 解码、NMS 和检测框绘制
Web 服务用 C++ MJPEG 输出实时画面
统计模块记录 FPS、延迟、队列、CPU、内存和 worker 数据
```

这个架构的重点是采集、推理、渲染、事件写入互相解耦，避免单线程阻塞。

## 4. 项目里遇到的主要困难

### 困难一：INT8 量化模型能跑但检测结果不对

一开始我用普通 YOLOv8 ONNX 转 INT8 RKNN，模型可以加载，NPU 也可以运行，但检测效果很差。后来我打印 RKNN 输出 tensor，发现 bbox 分支有值，但 class score 基本全是 0。说明问题不是 C++ 推理代码，而是量化后的分类分支出了问题。

解决办法是换成 Rockchip model-zoo 适配 YOLOv8 的 ONNX，并用视频帧生成校准集重新量化，最后得到有效的 INT8 RKNN 模型。

### 困难二：YOLOv8 输出格式不统一

普通 YOLOv8 ONNX 常见输出是 `[1,84,8400]`，但 Rockchip 优化后的 YOLOv8 是 9 个输出，分别对应三个尺度的 box DFL、class 和 score。

我在 C++ 中实现了 DFL softmax、box 解码、坐标还原、置信度筛选和 NMS，使项目同时支持 Rockchip 9 输出模型和常见单输出 YOLOv8 模型。

### 困难三：性能瓶颈不能靠猜

一开始 FPS 不高时，容易以为是 NPU 没跑满。但我读取板端 devfreq，发现 NPU 已经是 100%@1GHz。所以优化方向不是简单“拉高 NPU”，而是定位 CPU 后处理、Web JPEG 编码、模型量化和多 worker 并发。

最后通过 INT8 模型、三 RKNN context worker、减少输出拷贝、benchmark 跳过 Web 编码、后处理 score 预筛选，把性能提升到可用水平。

### 困难四：多线程 RKNN context 设计

RKNN context 不适合多个线程共享。为了并发压满 RK3588 三核 NPU，我让每个 inference worker 独立初始化自己的 `rknn_context`，并分别设置 core mask。这样比单线程 demo 更接近真实边缘部署。

### 困难五：实时性和吞吐的取舍

视频输入速度可能超过推理速度，如果无限排队，Web 预览会越来越滞后。我使用有界队列，队列满时丢弃旧帧，优先保证实时性。测试后发现 `queue_size=2` 是比较均衡的配置，队列更大不会明显提高 FPS，反而会增加延迟。

### 困难六：板端环境要克制

RK3588 板端可能不是自己的设备，不能随便安装大依赖或覆盖系统库。所以我把项目分成源码开发版和最小 runtime 包。最小包只包含二进制、配置、模型、Web 文件和测试视频，不包含源码、CMake 中间文件或 Python 环境。

## 5. 可以讲的性能数据

在测试板上，默认 INT8 YOLOv8n RKNN 模型，`num_workers=3`、`queue_size=2`：

```text
NPU load: 100%@1GHz
吞吐: 约 79 FPS
平均 NPU 推理: 约 22-24 ms
平均后处理: 约 1-2 ms
```

对比旧 FP 模型，三 worker 大约 32 FPS。正确 INT8 量化后吞吐提升明显。

注意表达时可以补一句：

> 这个 FPS 是视频文件快速读取下的吞吐 benchmark，不等同于真实摄像头帧率。真实 25/30 FPS 单路输入对系统压力更小。

## 6. 面试官可能追问

### 为什么不用 Python？

板端部署关注启动速度、资源占用和运行稳定性。Python 可以用于转换模型或工具脚本，但主服务用 C++ 更适合直接调用 RKNN Runtime 和控制多线程流水线。

### 为什么每个 worker 一个 rknn_context？

共享同一个 context 可能线程不安全，也可能被 Runtime 内部串行化。独立 context 可以让多个推理请求同时在飞，更适合 RK3588 三核 NPU 并发。

### 为什么要有界队列？

实时视频系统更关心最新帧。如果队列无限增长，延迟会越来越大。队列满时丢旧帧可以牺牲部分帧完整性，换取实时性。

### 为什么多 worker 不一定总是更快？

因为除了 NPU 推理，还有视频解码、预处理、后处理、内存带宽、调度开销和温度频率限制。超过硬件并发能力后，继续增加 worker 可能只会增加竞争。

### 如果换成自己的检测模型怎么办？

需要重新导出并转换 RKNN，更新 `classes.txt`，确认输入尺寸、letterbox padding 值和输出 tensor 格式。如果输出格式不同，需要改 `src/yolo_postprocess.cpp` 的 decode 部分。

## 7. 项目总结

可以这样收尾：

> 这个项目让我理解到，边缘 AI 部署不是把模型转成 RKNN 就结束了。真正难的是确认量化模型有效、适配不同输出格式、定位真实性能瓶颈、设计低延迟流水线，并且在不破坏板端环境的前提下做成可复现、可验证、可交付的系统。
