# Models

Place RKNN models here.

Default expected model:

```text
models/yolov8n.rknn
```

The default model is an RK3588 INT8 YOLOv8n RKNN converted from the Rockchip model-zoo YOLOv8 ONNX. It uses a 9-output YOLOv8 head: box DFL, class, and score outputs for strides 8, 16, and 32.

The C++ runtime also supports common single-output YOLOv8 layouts such as `[1,84,8400]` and `[1,8400,84]`. You can replace the model with a custom YOLOv8 RKNN model for safety helmet, vest, smoke/fire, steel defect, or other edge inspection tasks. If your exported RKNN output shape differs, update `src/yolo_postprocess.cpp`.
