#pragma once

#include "detector.hpp"
#include "utils.hpp"

#include <atomic>
#include <mutex>
#include <opencv2/opencv.hpp>
#include <string>
#include <thread>
#include <vector>

namespace edge {

struct EventRecord {
    std::string timestamp;
    std::string class_name;
    float confidence = 0.0f;
    Detection bbox;
    std::string image_path;
};

class EventManager {
public:
    EventManager(std::string event_dir, bool enabled, size_t recent_limit = 50);
    ~EventManager();

    bool start();
    void stop();
    void submit(uint64_t frame_id, const cv::Mat& frame, const std::vector<Detection>& detections);
    std::string recentEventsJson();
    size_t queueSize() const;

private:
    struct EventTask {
        uint64_t frame_id = 0;
        cv::Mat frame;
        std::vector<Detection> detections;
    };

    void loop();
    void remember(const EventRecord& record);
    std::string recordToJson(const EventRecord& record) const;

    std::string event_dir_;
    bool enabled_ = true;
    size_t recent_limit_ = 50;
    std::atomic<bool> running_{false};
    BoundedQueue<EventTask> queue_;
    std::thread thread_;
    mutable std::mutex recent_mutex_;
    std::vector<EventRecord> recent_;
};

}  // namespace edge
