#!/usr/bin/env python3
"""Convert YOLOv8n ONNX to RK3588 RKNN.

This is a host-side helper. It is not used by the board-side C++ service.
"""

from __future__ import annotations

import argparse
from pathlib import Path

import cv2
import numpy as np
from rknn.api import RKNN


def make_calibration_dataset(root: Path, count: int = 32) -> Path:
    image_dir = root / "tools" / "rknn" / "calibration_images"
    image_dir.mkdir(parents=True, exist_ok=True)
    rng = np.random.default_rng(3588)
    paths = []
    for i in range(count):
        img = np.zeros((640, 640, 3), dtype=np.uint8)
        base = rng.integers(0, 70, size=(1, 1, 3), dtype=np.uint8)
        img[:] = base
        for _ in range(10):
            color = tuple(int(x) for x in rng.integers(40, 255, size=3))
            x1, y1 = [int(x) for x in rng.integers(0, 560, size=2)]
            x2 = min(639, x1 + int(rng.integers(20, 160)))
            y2 = min(639, y1 + int(rng.integers(20, 160)))
            cv2.rectangle(img, (x1, y1), (x2, y2), color, -1)
        path = image_dir / f"calib_{i:03d}.jpg"
        cv2.imwrite(str(path), img)
        paths.append(path)

    dataset = root / "tools" / "rknn" / "dataset.txt"
    dataset.write_text("\n".join(str(p) for p in paths) + "\n", encoding="utf-8")
    return dataset


def letterbox_image(frame: np.ndarray, size: int = 640, pad_value: int = 0) -> np.ndarray:
    h, w = frame.shape[:2]
    scale = min(size / float(w), size / float(h))
    new_w = max(1, int(round(w * scale)))
    new_h = max(1, int(round(h * scale)))
    resized = cv2.resize(frame, (new_w, new_h), interpolation=cv2.INTER_LINEAR)
    out = np.full((size, size, 3), pad_value, dtype=np.uint8)
    x = (size - new_w) // 2
    y = (size - new_h) // 2
    out[y:y + new_h, x:x + new_w] = resized
    return out


def make_video_calibration_dataset(root: Path, video: Path, count: int = 64,
                                   letterbox_calib: bool = True, pad_value: int = 0) -> Path:
    image_dir = root / "tools" / "rknn" / "calibration_video_frames"
    image_dir.mkdir(parents=True, exist_ok=True)
    cap = cv2.VideoCapture(str(video))
    if not cap.isOpened():
        raise RuntimeError(f"Cannot open calibration video: {video}")
    total = int(cap.get(cv2.CAP_PROP_FRAME_COUNT) or 0)
    if total <= 0:
        total = count
    indices = np.linspace(0, max(0, total - 1), num=count, dtype=int)
    paths = []
    for i, idx in enumerate(indices):
        cap.set(cv2.CAP_PROP_POS_FRAMES, int(idx))
        ok, frame = cap.read()
        if not ok or frame is None:
            continue
        if letterbox_calib:
            frame = letterbox_image(frame, 640, pad_value)
        path = image_dir / f"video_calib_{i:03d}.jpg"
        cv2.imwrite(str(path), frame)
        paths.append(path)
    cap.release()
    if not paths:
        raise RuntimeError(f"No frames extracted from calibration video: {video}")
    dataset = root / "tools" / "rknn" / "dataset_video.txt"
    dataset.write_text("\n".join(str(p) for p in paths) + "\n", encoding="utf-8")
    return dataset


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--onnx", default="models/yolov8n.onnx")
    parser.add_argument("--output", default="models/yolov8n.rknn")
    parser.add_argument("--target", default="rk3588")
    parser.add_argument("--dataset", default="")
    parser.add_argument("--calib-video", default="")
    parser.add_argument("--calib-count", type=int, default=64)
    parser.add_argument("--pad-value", type=int, default=0)
    parser.add_argument("--no-letterbox-calib", action="store_true")
    parser.add_argument("--auto-load-onnx", action="store_true",
                        help="Do not force input names/sizes. Use this for Rockchip model-zoo YOLOv8 ONNX.")
    parser.add_argument("--no-quant", action="store_true")
    args = parser.parse_args()

    root = Path.cwd()
    onnx_path = root / args.onnx
    output_path = root / args.output
    if not onnx_path.exists():
        raise FileNotFoundError(f"ONNX not found: {onnx_path}")

    if args.dataset:
        dataset = Path(args.dataset)
    elif args.calib_video:
        dataset = make_video_calibration_dataset(root, root / args.calib_video, args.calib_count,
                                                 not args.no_letterbox_calib, args.pad_value)
    else:
        dataset = make_calibration_dataset(root)
    if not args.no_quant and not dataset.exists():
        raise FileNotFoundError(f"Calibration dataset not found: {dataset}")

    output_path.parent.mkdir(parents=True, exist_ok=True)

    rknn = RKNN(verbose=True)
    print(f"[convert] config target_platform={args.target}")
    ret = rknn.config(
        target_platform=args.target,
        mean_values=[[0, 0, 0]],
        std_values=[[255, 255, 255]],
        quant_img_RGB2BGR=False,
    )
    if ret != 0:
        raise RuntimeError(f"rknn.config failed: {ret}")

    print(f"[convert] load ONNX: {onnx_path}")
    if args.auto_load_onnx:
        ret = rknn.load_onnx(model=str(onnx_path))
    else:
        ret = rknn.load_onnx(
            model=str(onnx_path),
            inputs=["images"],
            input_size_list=[[1, 3, 640, 640]],
        )
    if ret != 0:
        raise RuntimeError(f"rknn.load_onnx failed: {ret}")

    print(f"[convert] build quant={not args.no_quant} dataset={dataset}")
    ret = rknn.build(do_quantization=not args.no_quant, dataset=str(dataset) if not args.no_quant else None)
    if ret != 0:
        raise RuntimeError(f"rknn.build failed: {ret}")

    print(f"[convert] export: {output_path}")
    ret = rknn.export_rknn(str(output_path))
    if ret != 0:
        raise RuntimeError(f"rknn.export_rknn failed: {ret}")
    rknn.release()
    print(f"[convert] done: {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
