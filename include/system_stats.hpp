#pragma once

#include "detector.hpp"

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace edge {

struct WorkerSnapshot {
    int id = 0;
    uint64_t frames = 0;
    double fps = 0.0;
    double avg_inference_ms = 0.0;
};

class SystemStats {
public:
    explicit SystemStats(int workers = 1);

    void recordFrame(const StageTimings& timings);
    void recordWorker(int worker_id, double inference_ms);
    void addEvents(uint64_t count);
    void setQueues(size_t capture_queue, size_t result_queue, size_t event_queue, uint64_t dropped);
    std::string toJson();
    uint64_t frameCount() const;

private:
    double readCpuPercent();
    double readMemoryMb();
    double avg(double total) const;

    mutable std::mutex mutex_;
    int workers_ = 1;
    uint64_t frame_count_ = 0;
    uint64_t event_count_ = 0;
    uint64_t dropped_frames_ = 0;
    size_t capture_queue_ = 0;
    size_t result_queue_ = 0;
    size_t event_queue_ = 0;
    double total_capture_ms_ = 0.0;
    double total_preprocess_ms_ = 0.0;
    double total_inference_ms_ = 0.0;
    double total_postprocess_ms_ = 0.0;
    double total_render_ms_ = 0.0;
    double total_pipeline_ms_ = 0.0;
    std::chrono::steady_clock::time_point start_;
    std::vector<uint64_t> worker_frames_;
    std::vector<double> worker_inference_ms_;
};

}  // namespace edge
