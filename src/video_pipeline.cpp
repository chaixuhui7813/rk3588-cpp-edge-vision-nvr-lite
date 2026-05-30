#include "video_pipeline.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <sstream>

namespace edge {

VideoPipeline::VideoPipeline(Config config, std::shared_ptr<Detector> detector, MjpegServer& server,
                             EventManager& events, MqttClient& mqtt, SystemStats& stats)
    : config_(std::move(config)),
      detector_(std::move(detector)),
      server_(server),
      events_(events),
      mqtt_(mqtt),
      stats_(stats),
      frame_queue_(static_cast<size_t>(config_.runtime.queue_size)),
      result_queue_(static_cast<size_t>(config_.runtime.queue_size)) {}

VideoPipeline::~VideoPipeline() {
    stop();
}

bool VideoPipeline::start() {
    if (!detector_) {
        std::cerr << "[pipeline] detector is null" << std::endl;
        return false;
    }
    running_.store(true);
    capture_thread_ = std::thread(&VideoPipeline::captureLoop, this);
    const int workers = std::max(1, std::min(config_.runtime.num_workers, 3));
    for (int i = 0; i < workers; ++i) {
        worker_threads_.emplace_back(&VideoPipeline::inferenceLoop, this, i);
    }
    render_thread_ = std::thread(&VideoPipeline::renderLoop, this);
    return true;
}

void VideoPipeline::stop() {
    running_.store(false);
    frame_queue_.notifyAll();
    result_queue_.notifyAll();
    if (capture_thread_.joinable()) capture_thread_.join();
    for (auto& t : worker_threads_) {
        if (t.joinable()) t.join();
    }
    if (render_thread_.joinable()) render_thread_.join();
}

bool VideoPipeline::openCapture(cv::VideoCapture& cap) {
    bool is_index = false;
    const int index = parseSourceIndex(config_.video.source, is_index);
    if (is_index) {
        cap.open(index);
    } else {
        cap.open(config_.video.source);
    }
    if (!cap.isOpened()) {
        std::cerr << "[capture] cannot open source: " << config_.video.source << std::endl;
        return false;
    }
    if (config_.video.width > 0) cap.set(cv::CAP_PROP_FRAME_WIDTH, config_.video.width);
    if (config_.video.height > 0) cap.set(cv::CAP_PROP_FRAME_HEIGHT, config_.video.height);
    return true;
}

void VideoPipeline::captureLoop() {
    cv::VideoCapture cap;
    uint64_t frame_id = 0;
    while (running_.load()) {
        if (!cap.isOpened() && !openCapture(cap)) {
            if (!config_.runtime.use_rknn) {
                const auto t0 = Clock::now();
                cv::Mat synthetic(config_.video.height, config_.video.width, CV_8UC3, cv::Scalar(32, 34, 42));
                const int x = static_cast<int>((frame_id * 7) % std::max<uint64_t>(1, config_.video.width));
                cv::circle(synthetic, cv::Point(x, config_.video.height / 2), 48, cv::Scalar(80, 160, 240), cv::FILLED);
                cv::putText(synthetic, "mock synthetic source", cv::Point(24, 48),
                            cv::FONT_HERSHEY_SIMPLEX, 0.9, cv::Scalar(230, 230, 230), 2);
                FramePacket packet;
                packet.frame_id = frame_id++;
                packet.frame = std::move(synthetic);
                packet.captured_at = Clock::now();
                packet.capture_ms = msSince(t0);
                frame_queue_.pushDropOldest(std::move(packet));
                updateQueueStats();
                sleepMs(33);
                continue;
            }
            sleepMs(1000);
            continue;
        }

        const auto t0 = Clock::now();
        cv::Mat frame;
        if (!cap.read(frame) || frame.empty()) {
            if (config_.runtime.loop_video) {
                cap.set(cv::CAP_PROP_POS_FRAMES, 0);
                sleepMs(50);
                continue;
            }
            std::cerr << "[capture] read failed" << std::endl;
            if (config_.video.reconnect) {
                cap.release();
                sleepMs(1000);
                continue;
            }
            running_.store(false);
            break;
        }

        FramePacket packet;
        packet.frame_id = frame_id++;
        packet.frame = std::move(frame);
        packet.captured_at = Clock::now();
        packet.capture_ms = msSince(t0);
        frame_queue_.pushDropOldest(std::move(packet));
        updateQueueStats();
    }
}

void VideoPipeline::inferenceLoop(int worker_id) {
    while (running_.load()) {
        FramePacket packet;
        if (!frame_queue_.popWait(packet, 200, running_)) {
            continue;
        }
        ResultPacket result;
        result.frame_id = packet.frame_id;
        result.frame = std::move(packet.frame);
        result.worker_id = worker_id;
        result.timings.capture_ms = packet.capture_ms;
        result.detections = detector_->detect(result.frame, worker_id, result.timings);
        result.timings.total_pipeline_ms = msSince(packet.captured_at);
        stats_.recordWorker(worker_id, result.timings.inference_ms);
        result_queue_.pushDropOldest(std::move(result));
        updateQueueStats();
    }
}

void VideoPipeline::renderLoop() {
    std::map<uint64_t, ResultPacket> pending;
    uint64_t next_id = 0;
    cv::VideoWriter preview_writer;
    bool preview_writer_failed = false;
    TimePoint last_web_frame = Clock::now() - std::chrono::milliseconds(config_.app.web_jpeg_interval_ms);
    while (running_.load()) {
        ResultPacket packet;
        if (result_queue_.popWait(packet, 100, running_)) {
            pending.emplace(packet.frame_id, std::move(packet));
        }
        if (pending.empty()) {
            continue;
        }

        auto it = pending.find(next_id);
        if (it == pending.end()) {
            if (pending.size() < static_cast<size_t>(config_.runtime.queue_size)) {
                continue;
            }
            it = pending.begin();
            next_id = it->first;
        }

        ResultPacket result = std::move(it->second);
        pending.erase(it);
        next_id = result.frame_id + 1;

        const auto render_start = Clock::now();
        drawDetections(result.frame, result.detections, result.timings);
        result.timings.render_ms = msSince(render_start);
        result.timings.total_pipeline_ms += result.timings.render_ms;
        if (config_.app.save_preview_video && !preview_writer_failed) {
            if (!preview_writer.isOpened()) {
                try {
                    std::filesystem::path out(config_.app.preview_video_path);
                    if (out.has_parent_path()) {
                        ensureDir(out.parent_path().string());
                    }
                } catch (...) {
                    ensureDir(config_.app.event_dir);
                }
                const int fourcc = cv::VideoWriter::fourcc('m', 'p', '4', 'v');
                preview_writer.open(config_.app.preview_video_path, fourcc, 25.0, result.frame.size(), true);
                if (!preview_writer.isOpened()) {
                    std::cerr << "[record] failed to open preview video: "
                              << config_.app.preview_video_path << std::endl;
                    preview_writer_failed = true;
                } else {
                    std::cout << "[record] writing annotated preview video: "
                              << config_.app.preview_video_path << std::endl;
                }
            }
            if (preview_writer.isOpened()) {
                preview_writer.write(result.frame);
            }
        }
        if (!config_.runtime.benchmark) {
            const auto now = Clock::now();
            const bool due = config_.app.web_jpeg_interval_ms <= 0 ||
                             std::chrono::duration_cast<std::chrono::milliseconds>(now - last_web_frame).count() >=
                                 config_.app.web_jpeg_interval_ms;
            if (due) {
                server_.updateFrame(result.frame);
                last_web_frame = now;
            }
        }
        events_.submit(result.frame_id, result.frame, result.detections);
        for (const auto& d : result.detections) {
            std::ostringstream payload;
            payload.setf(std::ios::fixed);
            payload.precision(3);
            payload << "{\"frame_id\":" << result.frame_id
                    << ",\"class_name\":\"" << jsonEscape(d.class_name) << "\""
                    << ",\"confidence\":" << d.confidence
                    << ",\"bbox\":[" << d.x1 << "," << d.y1 << "," << d.x2 << "," << d.y2 << "]}";
            mqtt_.publishEvent(payload.str());
        }
        stats_.addEvents(result.detections.size());
        stats_.recordFrame(result.timings);
        updateQueueStats();

        if (config_.runtime.benchmark && config_.runtime.benchmark_frames > 0 &&
            stats_.frameCount() >= static_cast<uint64_t>(config_.runtime.benchmark_frames)) {
            running_.store(false);
            frame_queue_.notifyAll();
            result_queue_.notifyAll();
            break;
        }
    }
}

void VideoPipeline::drawDetections(cv::Mat& frame, const std::vector<Detection>& detections, const StageTimings& timings) {
    for (const auto& d : detections) {
        cv::Scalar color(40, 220, 80);
        cv::rectangle(frame, cv::Point(static_cast<int>(d.x1), static_cast<int>(d.y1)),
                      cv::Point(static_cast<int>(d.x2), static_cast<int>(d.y2)), color, 2);
        std::ostringstream label;
        label.setf(std::ios::fixed);
        label.precision(2);
        label << d.class_name << " " << d.confidence;
        int baseline = 0;
        cv::Size size = cv::getTextSize(label.str(), cv::FONT_HERSHEY_SIMPLEX, 0.55, 1, &baseline);
        int x = std::max(0, static_cast<int>(d.x1));
        int y = std::max(size.height + 4, static_cast<int>(d.y1));
        cv::rectangle(frame, cv::Rect(x, y - size.height - 4, size.width + 6, size.height + 6), color, cv::FILLED);
        cv::putText(frame, label.str(), cv::Point(x + 3, y - 3), cv::FONT_HERSHEY_SIMPLEX, 0.55, cv::Scalar(0, 0, 0), 1);
    }
    if (config_.runtime.draw_fps) {
        std::ostringstream oss;
        oss.setf(std::ios::fixed);
        oss.precision(1);
        oss << "infer " << timings.inference_ms << " ms  total " << timings.total_pipeline_ms << " ms";
        cv::putText(frame, oss.str(), cv::Point(12, 28), cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(20, 230, 230), 2);
    }
}

void VideoPipeline::updateQueueStats() {
    stats_.setQueues(frame_queue_.size(), result_queue_.size(), events_.queueSize(),
                     frame_queue_.dropped() + result_queue_.dropped());
}

}  // namespace edge
