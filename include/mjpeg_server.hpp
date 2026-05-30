#pragma once

#include "event_manager.hpp"
#include "system_stats.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <opencv2/opencv.hpp>
#include <string>
#include <thread>
#include <vector>

namespace httplib {
class Server;
}

namespace edge {

class MjpegServer {
public:
    MjpegServer(int port, std::string detector_name, SystemStats& stats, EventManager& events);
    ~MjpegServer();

    bool start();
    void stop();
    void updateFrame(const cv::Mat& frame);

private:
    std::string loadWebFile(const std::string& relative);
    std::string healthJson() const;

    int port_;
    std::string detector_name_;
    SystemStats& stats_;
    EventManager& events_;
    std::atomic<bool> running_{false};
    std::thread thread_;
    std::unique_ptr<httplib::Server> server_impl_;
    std::mutex frame_mutex_;
    std::vector<uchar> latest_jpeg_;
};

}  // namespace edge
