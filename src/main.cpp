#include "config.hpp"
#include "detector.hpp"
#include "event_manager.hpp"
#include "mjpeg_server.hpp"
#include "mock_detector.hpp"
#include "mqtt_client.hpp"
#include "rknn_detector.hpp"
#include "system_stats.hpp"
#include "video_pipeline.hpp"

#include <atomic>
#include <csignal>
#include <iostream>
#include <memory>

namespace {
std::atomic<bool> g_running{true};

void onSignal(int) {
    g_running.store(false);
}
}  // namespace

int main(int argc, char** argv) {
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    edge::CliOptions options = edge::parseArgs(argc, argv);
    edge::Config config;
    std::string error;
    if (!edge::loadConfig(options.config_path, config, error)) {
        std::cerr << "[main] " << error << std::endl;
        return 1;
    }
    edge::applyCliOverrides(config, options);

    auto classes = edge::loadClassNames(config.model.classes);
    std::shared_ptr<edge::Detector> detector;
    if (config.runtime.use_rknn) {
        detector = std::make_shared<edge::RknnDetector>(config.model, config.runtime, classes);
    } else {
        detector = std::make_shared<edge::MockDetector>(config.model, classes);
    }

    if (!detector->init()) {
        std::cerr << "[main] detector init failed: " << detector->name() << std::endl;
        if (config.runtime.use_rknn) {
            std::cerr << "[main] use --mock to verify the full video/web/event pipeline without RKNN model" << std::endl;
        }
        return 2;
    }

    edge::EventManager events(config.app.event_dir, config.app.save_events);
    if (!events.start()) {
        std::cerr << "[main] event manager failed to start" << std::endl;
        return 3;
    }

    edge::SystemStats stats(config.runtime.num_workers);
    edge::MqttClient mqtt(config.mqtt);
    mqtt.start();

    edge::MjpegServer server(config.app.web_port, detector->name(), stats, events);
    server.start();

    edge::VideoPipeline pipeline(config, detector, server, events, mqtt, stats);
    if (!pipeline.start()) {
        std::cerr << "[main] pipeline failed to start" << std::endl;
        return 4;
    }

    std::cout << "[main] " << config.app.name << " running" << std::endl;
    std::cout << "[main] web: http://<board-ip>:" << config.app.web_port << std::endl;
    std::cout << "[main] detector=" << detector->name() << ", workers=" << config.runtime.num_workers
              << ", queue_size=" << config.runtime.queue_size << std::endl;

    while (g_running.load() && pipeline.running()) {
        edge::sleepMs(300);
    }

    pipeline.stop();
    events.stop();
    mqtt.stop();
    std::cout << "[main] final stats: " << stats.toJson() << std::endl;
    return 0;
}
