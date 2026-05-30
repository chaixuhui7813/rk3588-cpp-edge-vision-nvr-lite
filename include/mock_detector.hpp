#pragma once

#include "config.hpp"
#include "detector.hpp"

#include <atomic>

namespace edge {

class MockDetector : public Detector {
public:
    MockDetector(ModelConfig model, std::vector<std::string> classes);
    bool init() override;
    std::vector<Detection> detect(const cv::Mat& frame, int worker_id, StageTimings& timings) override;
    std::string name() const override { return "mock"; }

private:
    ModelConfig model_;
    std::vector<std::string> classes_;
    std::atomic<uint64_t> frame_counter_{0};
};

}  // namespace edge
