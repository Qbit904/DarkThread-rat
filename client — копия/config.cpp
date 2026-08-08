#include "config.h"
#include <fstream>

static std::string Trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

ClientConfig ClientConfig::Load(const std::string& path) {
    ClientConfig cfg;
    std::ifstream file(path);
    if (!file.is_open()) return cfg;

    std::string line;
    while (std::getline(file, line)) {
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = Trim(line.substr(0, eq));
        std::string val = Trim(line.substr(eq + 1));

        if (key == "host") cfg.host = val;
        else if (key == "port") {
            try { cfg.port = static_cast<uint16_t>(std::stoi(val)); }
            catch (...) {}
        }
    }
    return cfg;
}