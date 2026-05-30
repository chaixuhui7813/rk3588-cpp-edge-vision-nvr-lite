#pragma once

#include "detector.hpp"

#include <opencv2/opencv.hpp>

#include <string>
#include <vector>

namespace edge::yolo {

struct LetterboxInfo {
    float scale = 1.0f;
    int pad_x = 0;
    int pad_y = 0;
    int new_w = 0;
    int new_h = 0;
};

struct TensorOutput {
    std::vector<float> data;
    std::vector<int> dims;
};

struct TensorView {
    const float* data = nullptr;
    size_t count = 0;
    std::vector<int> dims;
};

cv::Mat letterbox(const cv::Mat& src, int input_w, int input_h, LetterboxInfo& info, int pad_value = 114);
std::vector<Detection> postprocessYolo(
    const std::vector<TensorOutput>& outputs,
    int input_w,
    int input_h,
    const cv::Size& original_size,
    const LetterboxInfo& letterbox,
    const std::vector<std::string>& class_names,
    float conf_threshold,
    float nms_threshold);
std::vector<Detection> postprocessYoloViews(
    const std::vector<TensorView>& outputs,
    int input_w,
    int input_h,
    const cv::Size& original_size,
    const LetterboxInfo& letterbox,
    const std::vector<std::string>& class_names,
    float conf_threshold,
    float nms_threshold);

std::vector<Detection> nmsDetections(const std::vector<Detection>& detections, float nms_threshold);

}  // namespace edge::yolo
