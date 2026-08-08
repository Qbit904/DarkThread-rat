#pragma once
#include <string>
#include <cstdint>

struct ClientConfig {
    std::string host = "127.0.0.1";
    uint16_t port = 9090;

    static ClientConfig Load(const std::string& path);
};