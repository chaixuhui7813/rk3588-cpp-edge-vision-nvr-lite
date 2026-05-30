#pragma once

#include <opencv2/opencv.hpp>

#include <string>
#include <vector>

namespace edge {

struct Detection {
    int class_id = -1;
    std::string class_name;
    float confidence = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;
    float x2 = 0.0f;
    float y2 = 0.0f;
};

struct StageTimings {
    double capture_ms = 0.0;
    double preprocess_ms = 0.0;
    double inference_ms = 0.0;
    double postprocess_ms = 0.0;
    double render_ms = 0.0;
    double total_pipeline_ms = 0.0;
};

class Detector {
public:
    virtual ~Detector() = default;
    virtual bool init() = 0;
    virtual std::vector<Detection> detect(const cv::Mat& frame, int worker_id, StageTimings& timings) = 0;
    virtual std::string name() const = 0;
};

std::vector<std::string> loadClassNames(const std::string& path);

}  // namespace edge
