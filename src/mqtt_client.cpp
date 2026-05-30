#include "mqtt_client.hpp"

#include <iostream>

namespace edge {

MqttClient::MqttClient(MqttConfig config) : config_(std::move(config)) {}

bool MqttClient::start() {
    if (!config_.enable) {
        std::cout << "[mqtt] disabled" << std::endl;
        return true;
    }
    std::cout << "[mqtt] placeholder enabled for " << config_.host << ":" << config_.port
              << " topic=" << config_.topic << std::endl;
    std::cout << "[mqtt] build stays dependency-free; wire mosquitto/paho here if needed" << std::endl;
    return true;
}

void MqttClient::stop() {}

void MqttClient::publishEvent(const std::string& payload) {
    if (config_.enable) {
        std::cout << "[mqtt] publish placeholder: " << payload << std::endl;
    }
}

}  // namespace edge
