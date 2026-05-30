#pragma once

#include <string>
#include <vector>

namespace edge {

struct AppConfig {
    std::string name = "rk3588-cpp-edge-vision-nvr-lite";
    int web_port = 8080;
    bool save_events = true;
    std::string event_dir = "data/events";
    bool save_preview_video = true;
    std::string preview_video_path = "data/events/annotated_preview.mp4";
    int web_jpeg_interval_ms = 80;
};

struct VideoConfig {
    std::string source = "video/test.mp4";
    int width = 640;
    int height = 480;
    bool reconnect = true;
};

struct ModelConfig {
    std::string path = "models/yolov8n.rknn";
    int input_width = 640;
    int input_height = 640;
    std::string classes = "classes.txt";
    float conf_threshold = 0.25f;
    float nms_threshold = 0.45f;
    int letterbox_value = 114;
};

struct RuntimeConfig {
    bool use_rknn = true;
    int npu_core_mask = 7;
    int worker0_core_mask = 1;
    int worker1_core_mask = 2;
    int worker2_core_mask = 4;
    bool use_all_cores = false;
    int num_workers = 1;
    int queue_size = 8;
    bool loop_video = true;
    bool draw_fps = true;
    bool benchmark = false;
    int benchmark_frames = 0;
};

struct MqttConfig {
    bool enable = false;
    std::string host = "127.0.0.1";
    int port = 1883;
    std::string topic = "rk3588/events";
};

struct Config {
    AppConfig app;
    VideoConfig video;
    ModelConfig model;
    RuntimeConfig runtime;
    MqttConfig mqtt;
};

struct CliOptions {
    std::string config_path = "config.yaml";
    bool mock = false;
    std::string source_override;
    std::string model_override;
    std::string classes_override;
    int port_override = 0;
    bool benchmark = false;
};

bool loadConfig(const std::string& path, Config& config, std::string& error);
void applyCliOverrides(Config& config, const CliOptions& options);
CliOptions parseArgs(int argc, char** argv);

}  // namespace edge
