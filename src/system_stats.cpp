#include "system_stats.hpp"

#include "utils.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <unistd.h>

namespace edge {

SystemStats::SystemStats(int workers)
    : workers_(std::max(1, workers)),
      start_(std::chrono::steady_clock::now()),
      worker_frames_(workers_, 0),
      worker_inference_ms_(workers_, 0.0) {}

void SystemStats::recordFrame(const StageTimings& timings) {
    std::lock_guard<std::mutex> lock(mutex_);
    ++frame_count_;
    total_capture_ms_ += timings.capture_ms;
    total_preprocess_ms_ += timings.preprocess_ms;
    total_inference_ms_ += timings.inference_ms;
    total_postprocess_ms_ += timings.postprocess_ms;
    total_render_ms_ += timings.render_ms;
    total_pipeline_ms_ += timings.total_pipeline_ms;
}

void SystemStats::recordWorker(int worker_id, double inference_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (worker_id < 0 || worker_id >= workers_) {
        return;
    }
    ++worker_frames_[worker_id];
    worker_inference_ms_[worker_id] += inference_ms;
}

void SystemStats::addEvents(uint64_t count) {
    std::lock_guard<std::mutex> lock(mutex_);
    event_count_ += count;
}

void SystemStats::setQueues(size_t capture_queue, size_t result_queue, size_t event_queue, uint64_t dropped) {
    std::lock_guard<std::mutex> lock(mutex_);
    capture_queue_ = capture_queue;
    result_queue_ = result_queue;
    event_queue_ = event_queue;
    dropped_frames_ = dropped;
}

double SystemStats::avg(double total) const {
    return frame_count_ == 0 ? 0.0 : total / static_cast<double>(frame_count_);
}

std::string SystemStats::toJson() {
    std::lock_guard<std::mutex> lock(mutex_);
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count();
    const double fps = elapsed > 0.0 ? frame_count_ / elapsed : 0.0;
    std::ostringstream oss;
    oss.setf(std::ios::fixed);
    oss.precision(2);
    oss << "{";
    oss << "\"fps\":" << fps << ",";
    oss << "\"avg_inference_ms\":" << avg(total_inference_ms_) << ",";
    oss << "\"avg_npu_inference_ms\":" << avg(total_inference_ms_) << ",";
    oss << "\"avg_postprocess_ms\":" << avg(total_postprocess_ms_) << ",";
    oss << "\"avg_total_ms\":" << avg(total_pipeline_ms_) << ",";
    oss << "\"avg_end_to_end_ms\":" << avg(total_pipeline_ms_) << ",";
    oss << "\"avg_capture_ms\":" << avg(total_capture_ms_) << ",";
    oss << "\"avg_preprocess_ms\":" << avg(total_preprocess_ms_) << ",";
    oss << "\"avg_render_ms\":" << avg(total_render_ms_) << ",";
    oss << "\"frame_count\":" << frame_count_ << ",";
    oss << "\"event_count\":" << event_count_ << ",";
    oss << "\"cpu_percent\":" << readCpuPercent() << ",";
    oss << "\"memory_mb\":" << readMemoryMb() << ",";
    oss << "\"capture_queue\":" << capture_queue_ << ",";
    oss << "\"result_queue\":" << result_queue_ << ",";
    oss << "\"event_queue\":" << event_queue_ << ",";
    oss << "\"dropped_frames\":" << dropped_frames_ << ",";
    oss << "\"queues\":{";
    oss << "\"capture\":" << capture_queue_ << ",\"result\":" << result_queue_
        << ",\"event\":" << event_queue_ << ",\"dropped_frames\":" << dropped_frames_ << "},";
    oss << "\"workers\":[";
    for (int i = 0; i < workers_; ++i) {
        if (i) oss << ",";
        const double worker_fps = elapsed > 0.0 ? worker_frames_[i] / elapsed : 0.0;
        const double avg_inf = worker_frames_[i] == 0 ? 0.0 : worker_inference_ms_[i] / static_cast<double>(worker_frames_[i]);
        oss << "{\"id\":" << i << ",\"frames\":" << worker_frames_[i]
            << ",\"fps\":" << worker_fps << ",\"avg_inference_ms\":" << avg_inf << "}";
    }
    oss << "]}";
    return oss.str();
}

uint64_t SystemStats::frameCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return frame_count_;
}

double SystemStats::readCpuPercent() {
    // Lightweight fallback: process CPU since start, normalized by wall time and CPU count.
    try {
        std::ifstream stat("/proc/self/stat");
        if (!stat) return 0.0;
        std::string token;
        long utime = 0;
        long stime = 0;
        for (int i = 1; i <= 15 && stat >> token; ++i) {
            if (i == 14) utime = std::stol(token);
            if (i == 15) stime = std::stol(token);
        }
        const long ticks = sysconf(_SC_CLK_TCK);
        const long cores = std::max(1L, sysconf(_SC_NPROCESSORS_ONLN));
        const double cpu_seconds = static_cast<double>(utime + stime) / std::max(1L, ticks);
        const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count();
        return elapsed > 0.0 ? (cpu_seconds / elapsed) * 100.0 / static_cast<double>(cores) : 0.0;
    } catch (...) {
        return 0.0;
    }
}

double SystemStats::readMemoryMb() {
    std::ifstream status("/proc/self/status");
    std::string key;
    double kb = 0.0;
    while (status >> key) {
        if (key == "VmRSS:") {
            status >> kb;
            return kb / 1024.0;
        }
        std::string rest;
        std::getline(status, rest);
    }
    return 0.0;
}

}  // namespace edge
