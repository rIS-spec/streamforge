#pragma once
#include <string>
#include <cstdint>
#include <cstring>
#include <vector>
#include <arpa/inet.h>

constexpr uint32_t MAGIC_NUMBER = 0x53465247; // "SFRG" in hex

std::vector<uint8_t> encodePacket(const std::string& payload) {
    std::vector<uint8_t> packet;

    uint32_t magic_net = htonl(MAGIC_NUMBER);
    uint32_t len_net = htonl(static_cast<uint32_t>(payload.size()));

    const uint8_t* magic_bytes = reinterpret_cast<const uint8_t*>(&magic_net);
    packet.insert(packet.end(), magic_bytes, magic_bytes + 4);

    const uint8_t* len_bytes = reinterpret_cast<const uint8_t*>(&len_net);
    packet.insert(packet.end(), len_bytes, len_bytes + 4);

    packet.insert(packet.end(), payload.begin(), payload.end());

    return packet;
}

struct DecodedPacket {
    bool valid;
    std::string payload;
};

DecodedPacket decodePacket(const uint8_t* data, size_t data_len) {
    if (data_len < 8) {
        return {false, ""};
    }

    uint32_t magic_net, len_net;
    std::memcpy(&magic_net, data, 4);
    std::memcpy(&len_net, data + 4, 4);

    uint32_t magic = ntohl(magic_net);
    uint32_t length = ntohl(len_net);

    if (magic != MAGIC_NUMBER) {
        return {false, ""};
    }

    if (data_len < 8 + length) {
        return {false, ""};
    }

    std::string payload(reinterpret_cast<const char*>(data + 8), length);
    return {true, payload};
}