#include "utils.hpp"

#include <cctype>
#include <fstream>
#include <iostream>
#include <thread>

namespace edge {

double msSince(const TimePoint& start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

std::string nowTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_r(&t, &tm);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

std::string timestampForFile() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
    std::tm tm{};
    localtime_r(&t, &tm);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y%m%d_%H%M%S") << "_" << std::setw(3) << std::setfill('0') << ms;
    return oss.str();
}

std::string jsonEscape(const std::string& value) {
    std::ostringstream oss;
    for (char c : value) {
        switch (c) {
            case '\\': oss << "\\\\"; break;
            case '"': oss << "\\\""; break;
            case '\n': oss << "\\n"; break;
            case '\r': oss << "\\r"; break;
            case '\t': oss << "\\t"; break;
            default: oss << c; break;
        }
    }
    return oss.str();
}

bool ensureDir(const std::string& path) {
    try {
        if (path.empty()) {
            return false;
        }
        std::filesystem::create_directories(path);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[utils] failed to create directory " << path << ": " << e.what() << std::endl;
        return false;
    }
}

bool fileExists(const std::string& path) {
    return std::filesystem::exists(path);
}

std::string readTextFile(const std::string& path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
        return {};
    }
    std::ostringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}

int parseSourceIndex(const std::string& source, bool& is_index) {
    is_index = !source.empty();
    for (char c : source) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            is_index = false;
            return -1;
        }
    }
    return is_index ? std::stoi(source) : -1;
}

void sleepMs(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

}  // namespace edge
