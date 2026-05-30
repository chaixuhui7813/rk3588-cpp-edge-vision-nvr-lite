#pragma once

#include "config.hpp"
#include "detector.hpp"
#include "yolo_postprocess.hpp"

#include <mutex>

namespace edge {

class RknnDetector : public Detector {
public:
    RknnDetector(ModelConfig model, RuntimeConfig runtime, std::vector<std::string> classes);
    ~RknnDetector() override;

    bool init() override;
    std::vector<Detection> detect(const cv::Mat& frame, int worker_id, StageTimings& timings) override;
    std::string name() const override { return available_ ? "rknn" : "rknn-unavailable"; }

private:
    ModelConfig model_;
    RuntimeConfig runtime_;
    std::vector<std::string> classes_;
    bool available_ = false;

    struct Impl;
    Impl* impl_ = nullptr;
};

}  // namespace edge
