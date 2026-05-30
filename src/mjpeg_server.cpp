#include "mjpeg_server.hpp"

#include "utils.hpp"
#include "httplib.h"

#include <iostream>

namespace edge {

MjpegServer::MjpegServer(int port, std::string detector_name, SystemStats& stats, EventManager& events)
    : port_(port), detector_name_(std::move(detector_name)), stats_(stats), events_(events) {}

MjpegServer::~MjpegServer() {
    stop();
}

bool MjpegServer::start() {
    running_.store(true);
    server_impl_ = std::make_unique<httplib::Server>();
    thread_ = std::thread([this] {
        auto& svr = *server_impl_;
        svr.Get("/", [this](const httplib::Request&, httplib::Response& res) {
            res.set_content(loadWebFile("index.html"), "text/html; charset=utf-8");
        });
        svr.Get("/app.js", [this](const httplib::Request&, httplib::Response& res) {
            res.set_content(loadWebFile("app.js"), "application/javascript; charset=utf-8");
        });
        svr.Get("/style.css", [this](const httplib::Request&, httplib::Response& res) {
            res.set_content(loadWebFile("style.css"), "text/css; charset=utf-8");
        });
        svr.Get("/api/health", [this](const httplib::Request&, httplib::Response& res) {
            res.set_content(healthJson(), "application/json");
        });
        svr.Get("/api/stats", [this](const httplib::Request&, httplib::Response& res) {
            res.set_content(stats_.toJson(), "application/json");
        });
        svr.Get("/api/events", [this](const httplib::Request&, httplib::Response& res) {
            res.set_content(events_.recentEventsJson(), "application/json");
        });
        svr.GetStream("/api/video_feed", [this](int fd) {
            const std::string header =
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n"
                "Cache-Control: no-cache\r\n"
                "Connection: close\r\n\r\n";
            if (!httplib::Server::writeAll(fd, header)) {
                return;
            }
            while (running_.load()) {
                std::vector<uchar> jpg;
                {
                    std::lock_guard<std::mutex> lock(frame_mutex_);
                    jpg = latest_jpeg_;
                }
                if (!jpg.empty()) {
                    std::string part = "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: " +
                                       std::to_string(jpg.size()) + "\r\n\r\n";
                    if (!httplib::Server::writeAll(fd, part) ||
                        !httplib::Server::writeAll(fd, jpg.data(), jpg.size()) ||
                        !httplib::Server::writeAll(fd, "\r\n")) {
                        break;
                    }
                }
                sleepMs(80);
            }
        });
        std::cout << "[web] listening on http://0.0.0.0:" << port_ << std::endl;
        if (!svr.listen("0.0.0.0", port_)) {
            std::cerr << "[web] server stopped or failed" << std::endl;
        }
    });
    return true;
}

void MjpegServer::stop() {
    running_.store(false);
    if (server_impl_) {
        server_impl_->stop();
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    server_impl_.reset();
}

void MjpegServer::updateFrame(const cv::Mat& frame) {
    if (frame.empty()) {
        return;
    }
    std::vector<uchar> jpg;
    std::vector<int> params = {cv::IMWRITE_JPEG_QUALITY, 82};
    if (!cv::imencode(".jpg", frame, jpg, params)) {
        return;
    }
    std::lock_guard<std::mutex> lock(frame_mutex_);
    latest_jpeg_ = std::move(jpg);
}

std::string MjpegServer::loadWebFile(const std::string& relative) {
    std::string data = readTextFile("web/" + relative);
    if (data.empty() && relative == "index.html") {
        data = "<!doctype html><html><body><h1>edge_vision_nvr</h1><img src='/api/video_feed'></body></html>";
    }
    return data;
}

std::string MjpegServer::healthJson() const {
    return "{\"status\":\"ok\",\"backend\":\"cpp\",\"platform\":\"rk3588\",\"detector\":\"" +
           jsonEscape(detector_name_) + "\"}";
}

}  // namespace edge
