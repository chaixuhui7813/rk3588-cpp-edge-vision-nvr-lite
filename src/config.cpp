#include "config.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>

namespace edge {
namespace {

std::string trim(const std::string& s) {
    size_t begin = 0;
    while (begin < s.size() && std::isspace(static_cast<unsigned char>(s[begin]))) {
        ++begin;
    }
    size_t end = s.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }
    return s.substr(begin, end - begin);
}

std::string stripQuotes(std::string value) {
    value = trim(value);
    if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') ||
                              (value.front() == '\'' && value.back() == '\''))) {
        return value.substr(1, value.size() - 2);
    }
    return value;
}

bool parseBool(const std::string& value) {
    std::string v = value;
    std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return std::tolower(c); });
    return v == "true" || v == "1" || v == "yes" || v == "on";
}

void assign(Config& c, const std::string& section, const std::string& key, const std::string& raw) {
    const std::string value = stripQuotes(raw);
    try {
        if (section == "app") {
            if (key == "name") c.app.name = value;
            else if (key == "web_port") c.app.web_port = std::stoi(value);
            else if (key == "save_events") c.app.save_events = parseBool(value);
            else if (key == "event_dir") c.app.event_dir = value;
            else if (key == "save_preview_video") c.app.save_preview_video = parseBool(value);
            else if (key == "preview_video_path") c.app.preview_video_path = value;
            else if (key == "web_jpeg_interval_ms") c.app.web_jpeg_interval_ms = std::max(0, std::stoi(value));
        } else if (section == "video") {
            if (key == "source") c.video.source = value;
            else if (key == "width") c.video.width = std::stoi(value);
            else if (key == "height") c.video.height = std::stoi(value);
            else if (key == "reconnect") c.video.reconnect = parseBool(value);
        } else if (section == "model") {
            if (key == "path") c.model.path = value;
            else if (key == "input_width") c.model.input_width = std::stoi(value);
            else if (key == "input_height") c.model.input_height = std::stoi(value);
            else if (key == "classes") c.model.classes = value;
            else if (key == "conf_threshold") c.model.conf_threshold = std::stof(value);
            else if (key == "nms_threshold") c.model.nms_threshold = std::stof(value);
            else if (key == "letterbox_value") c.model.letterbox_value = std::max(0, std::min(255, std::stoi(value)));
        } else if (section == "runtime") {
            if (key == "use_rknn") c.runtime.use_rknn = parseBool(value);
            else if (key == "npu_core_mask") c.runtime.npu_core_mask = std::stoi(value);
            else if (key == "worker0_core_mask") c.runtime.worker0_core_mask = std::stoi(value);
            else if (key == "worker1_core_mask") c.runtime.worker1_core_mask = std::stoi(value);
            else if (key == "worker2_core_mask") c.runtime.worker2_core_mask = std::stoi(value);
            else if (key == "use_all_cores") c.runtime.use_all_cores = parseBool(value);
            else if (key == "num_workers") c.runtime.num_workers = std::max(1, std::stoi(value));
            else if (key == "queue_size") c.runtime.queue_size = std::max(1, std::stoi(value));
            else if (key == "loop_video") c.runtime.loop_video = parseBool(value);
            else if (key == "draw_fps") c.runtime.draw_fps = parseBool(value);
            else if (key == "benchmark") c.runtime.benchmark = parseBool(value);
            else if (key == "benchmark_frames") c.runtime.benchmark_frames = std::stoi(value);
        } else if (section == "mqtt") {
            if (key == "enable") c.mqtt.enable = parseBool(value);
            else if (key == "host") c.mqtt.host = value;
            else if (key == "port") c.mqtt.port = std::stoi(value);
            else if (key == "topic") c.mqtt.topic = value;
        }
    } catch (const std::exception& e) {
        std::cerr << "[config] invalid value " << section << "." << key << "=" << value
                  << ": " << e.what() << std::endl;
    }
}

}  // namespace

bool loadConfig(const std::string& path, Config& config, std::string& error) {
    std::ifstream ifs(path);
    if (!ifs) {
        error = "cannot open config file: " + path;
        return false;
    }
    std::string section;
    std::string line;
    int line_no = 0;
    while (std::getline(ifs, line)) {
        ++line_no;
        auto hash = line.find('#');
        if (hash != std::string::npos) {
            line = line.substr(0, hash);
        }
        line = trim(line);
        if (line.empty()) {
            continue;
        }
        if (line.back() == ':') {
            section = trim(line.substr(0, line.size() - 1));
            continue;
        }
        auto colon = line.find(':');
        if (colon == std::string::npos) {
            std::cerr << "[config] skip malformed line " << line_no << ": " << line << std::endl;
            continue;
        }
        assign(config, section, trim(line.substr(0, colon)), trim(line.substr(colon + 1)));
    }
    config.runtime.num_workers = std::max(1, std::min(config.runtime.num_workers, 3));
    config.runtime.queue_size = std::max(1, config.runtime.queue_size);
    return true;
}

void applyCliOverrides(Config& config, const CliOptions& options) {
    if (!options.source_override.empty()) config.video.source = options.source_override;
    if (!options.model_override.empty()) config.model.path = options.model_override;
    if (!options.classes_override.empty()) config.model.classes = options.classes_override;
    if (options.port_override > 0) config.app.web_port = options.port_override;
    if (options.mock) config.runtime.use_rknn = false;
    if (options.benchmark) config.runtime.benchmark = true;
}

CliOptions parseArgs(int argc, char** argv) {
    CliOptions options;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "[args] missing value for " << arg << std::endl;
                return {};
            }
            return argv[++i];
        };
        if (arg == "--config") options.config_path = next();
        else if (arg == "--mock") options.mock = true;
        else if (arg == "--source") options.source_override = next();
        else if (arg == "--model") options.model_override = next();
        else if (arg == "--classes") options.classes_override = next();
        else if (arg == "--port") options.port_override = std::stoi(next());
        else if (arg == "--benchmark") options.benchmark = true;
        else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: edge_vision_nvr --config config.yaml [--mock] [--source path|rtsp|0]\n"
                         "                       [--model models/yolov8n.rknn] [--classes classes.txt]\n"
                         "                       [--port 8080] [--benchmark]\n";
            std::exit(0);
        } else {
            std::cerr << "[args] unknown option: " << arg << std::endl;
        }
    }
    return options;
}

}  // namespace edge
