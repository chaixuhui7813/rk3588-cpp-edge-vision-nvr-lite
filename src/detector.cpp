#include "detector.hpp"

#include <fstream>

namespace edge {

std::vector<std::string> loadClassNames(const std::string& path) {
    std::vector<std::string> names;
    std::ifstream ifs(path);
    std::string line;
    while (std::getline(ifs, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (!line.empty()) {
            names.push_back(line);
        }
    }
    if (names.empty()) {
        names = {"object"};
    }
    return names;
}

}  // namespace edge
