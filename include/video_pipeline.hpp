#pragma once

#include "config.hpp"
#include "detector.hpp"
#include "event_manager.hpp"
#include "mjpeg_server.hpp"
#include "mqtt_client.hpp"
#include "system_stats.hpp"
#include "utils.hpp"

#include <atomic>
#include <map>
#include <memory>
#include <opencv2/opencv.hpp>
#include <thread>
#include <vector>

namespace edge {

struct FramePacket {
    uint64_t frame_id = 0;
    cv::Mat frame;
    TimePoint captured_at;
    double capture_ms = 0.0;
};

struct ResultPacket {
    uint64_t frame_id = 0;
    cv::Mat frame;
    std::vector<Detection> detections;
    StageTimings timings;
    int worker_id = 0;
};

class VideoPipeline {
public:
    VideoPipeline(Config config, std::shared_ptr<Detector> detector, MjpegServer& server,
                  EventManager& events, MqttClient& mqtt, SystemStats& stats);
    ~VideoPipeline();

    bool start();
    void stop();
    bool running() const { return running_.load(); }

private:
    void captureLoop();
    void inferenceLoop(int worker_id);
    void renderLoop();
    void drawDetections(cv::Mat& frame, const std::vector<Detection>& detections, const StageTimings& timings);
    bool openCapture(cv::VideoCapture& cap);
    void updateQueueStats();

    Config config_;
    std::shared_ptr<Detector> detector_;
    MjpegServer& server_;
    EventManager& events_;
    MqttClient& mqtt_;
    SystemStats& stats_;
    std::atomic<bool> running_{false};
    BoundedQueue<FramePacket> frame_queue_;
    BoundedQueue<ResultPacket> result_queue_;
    std::thread capture_thread_;
    std::vector<std::thread> worker_threads_;
    std::thread render_thread_;
};

}  // namespace edge
