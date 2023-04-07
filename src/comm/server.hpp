#pragma once

#include <cstdint>
#include <string>

struct eth_addr_t {
    uint8_t addr_bytes[6];
};

struct Server {
    std::string ip;
    uint16_t port;
    eth_addr_t mac;
};
