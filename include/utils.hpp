#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>

namespace edge {

using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;

double msSince(const TimePoint& start);
std::string nowTimestamp();
std::string timestampForFile();
std::string jsonEscape(const std::string& value);
bool ensureDir(const std::string& path);
bool fileExists(const std::string& path);
std::string readTextFile(const std::string& path);
int parseSourceIndex(const std::string& source, bool& is_index);
void sleepMs(int ms);

template <typename T>
class BoundedQueue {
public:
    explicit BoundedQueue(size_t max_size = 8) : max_size_(max_size == 0 ? 1 : max_size) {}

    bool pushDropOldest(T item) {
        std::lock_guard<std::mutex> lock(mutex_);
        bool dropped = false;
        if (queue_.size() >= max_size_) {
            queue_.pop_front();
            ++dropped_;
            dropped = true;
        }
        queue_.push_back(std::move(item));
        cv_.notify_one();
        return dropped;
    }

    bool popWait(T& out, int timeout_ms, const std::atomic<bool>& running) {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [&] {
            return !queue_.empty() || !running.load();
        });
        if (queue_.empty()) {
            return false;
        }
        out = std::move(queue_.front());
        queue_.pop_front();
        return true;
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    size_t dropped() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return dropped_;
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.clear();
    }

    void notifyAll() {
        cv_.notify_all();
    }

private:
    size_t max_size_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<T> queue_;
    size_t dropped_ = 0;
};

}  // namespace edge
