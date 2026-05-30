#include "mock_detector.hpp"

#include "utils.hpp"

#include <algorithm>
#include <cmath>

namespace edge {

MockDetector::MockDetector(ModelConfig model, std::vector<std::string> classes)
    : model_(std::move(model)), classes_(std::move(classes)) {}

bool MockDetector::init() {
    if (classes_.empty()) {
        classes_.push_back("object");
    }
    return true;
}

std::vector<Detection> MockDetector::detect(const cv::Mat& frame, int worker_id, StageTimings& timings) {
    const auto t0 = Clock::now();
    sleepMs(1);
    timings.preprocess_ms = msSince(t0);

    const auto t1 = Clock::now();
    sleepMs(6 + (worker_id % 3));
    timings.inference_ms = msSince(t1);

    const auto t2 = Clock::now();
    const uint64_t id = frame_counter_.fetch_add(1);
    const int w = std::max(80, frame.cols / 5);
    const int h = std::max(80, frame.rows / 4);
    const int max_x = std::max(1, frame.cols - w - 1);
    const int max_y = std::max(1, frame.rows - h - 1);
    const float phase = static_cast<float>((id * 13 + worker_id * 37) % 360) * 3.1415926f / 180.0f;
    const int x = static_cast<int>((std::sin(phase) * 0.5f + 0.5f) * max_x);
    const int y = static_cast<int>((std::cos(phase * 0.7f) * 0.5f + 0.5f) * max_y);

    Detection d;
    d.class_id = 0;
    d.class_name = classes_.empty() ? "mock_object" : classes_[0];
    d.confidence = std::max(model_.conf_threshold, 0.80f);
    d.x1 = static_cast<float>(x);
    d.y1 = static_cast<float>(y);
    d.x2 = static_cast<float>(x + w);
    d.y2 = static_cast<float>(y + h);
    timings.postprocess_ms = msSince(t2);
    return {d};
}

}  // namespace edge
