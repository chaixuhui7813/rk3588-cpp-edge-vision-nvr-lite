#pragma once

#include "config.hpp"

#include <string>

namespace edge {

class MqttClient {
public:
    explicit MqttClient(MqttConfig config);
    bool start();
    void stop();
    void publishEvent(const std::string& payload);
    bool enabled() const { return config_.enable; }

private:
    MqttConfig config_;
};

}  // namespace edge
