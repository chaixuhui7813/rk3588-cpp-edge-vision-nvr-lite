#include "event_manager.hpp"

#include <fstream>
#include <iostream>
#include <sstream>

namespace edge {

EventManager::EventManager(std::string event_dir, bool enabled, size_t recent_limit)
    : event_dir_(std::move(event_dir)), enabled_(enabled), recent_limit_(recent_limit), queue_(32) {}

EventManager::~EventManager() {
    stop();
}

bool EventManager::start() {
    if (!enabled_) {
        return true;
    }
    if (!ensureDir(event_dir_)) {
        return false;
    }
    running_.store(true);
    thread_ = std::thread(&EventManager::loop, this);
    return true;
}

void EventManager::stop() {
    running_.store(false);
    queue_.notifyAll();
    if (thread_.joinable()) {
        thread_.join();
    }
}

void EventManager::submit(uint64_t frame_id, const cv::Mat& frame, const std::vector<Detection>& detections) {
    if (!enabled_ || detections.empty() || frame.empty()) {
        return;
    }
    EventTask task;
    task.frame_id = frame_id;
    task.frame = frame.clone();
    task.detections = detections;
    queue_.pushDropOldest(std::move(task));
}

std::string EventManager::recentEventsJson() {
    std::lock_guard<std::mutex> lock(recent_mutex_);
    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < recent_.size(); ++i) {
        if (i) oss << ",";
        oss << recordToJson(recent_[recent_.size() - 1 - i]);
    }
    oss << "]";
    return oss.str();
}

size_t EventManager::queueSize() const {
    return queue_.size();
}

void EventManager::loop() {
    while (running_.load()) {
        EventTask task;
        if (!queue_.popWait(task, 200, running_)) {
            continue;
        }
        const std::string image_path = event_dir_ + "/" + timestampForFile() + "_" + std::to_string(task.frame_id) + ".jpg";
        if (!cv::imwrite(image_path, task.frame)) {
            std::cerr << "[events] failed to write " << image_path << std::endl;
            continue;
        }
        std::ofstream log(event_dir_ + "/events.jsonl", std::ios::app);
        for (const auto& det : task.detections) {
            EventRecord record;
            record.timestamp = nowTimestamp();
            record.class_name = det.class_name;
            record.confidence = det.confidence;
            record.bbox = det;
            record.image_path = image_path;
            remember(record);
            if (log) {
                log << recordToJson(record) << "\n";
            }
        }
    }
}

void EventManager::remember(const EventRecord& record) {
    std::lock_guard<std::mutex> lock(recent_mutex_);
    recent_.push_back(record);
    if (recent_.size() > recent_limit_) {
        recent_.erase(recent_.begin(), recent_.begin() + static_cast<long>(recent_.size() - recent_limit_));
    }
}

std::string EventManager::recordToJson(const EventRecord& record) const {
    std::ostringstream oss;
    oss.setf(std::ios::fixed);
    oss.precision(3);
    oss << "{";
    oss << "\"timestamp\":\"" << jsonEscape(record.timestamp) << "\",";
    oss << "\"class_name\":\"" << jsonEscape(record.class_name) << "\",";
    oss << "\"confidence\":" << record.confidence << ",";
    oss << "\"bbox\":[" << record.bbox.x1 << "," << record.bbox.y1 << ","
        << record.bbox.x2 << "," << record.bbox.y2 << "],";
    oss << "\"image_path\":\"" << jsonEscape(record.image_path) << "\"";
    oss << "}";
    return oss.str();
}

}  // namespace edge
