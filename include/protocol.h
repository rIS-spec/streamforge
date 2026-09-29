#pragma once
#include <string>
#include <cstdint>
#include <cstring>
#include <vector>
#include <array>
#include <arpa/inet.h>

constexpr uint32_t MAGIC_NUMBER = 0x53465247;  // "SFRG" in hex
constexpr uint8_t PROTOCOL_VERSION = 1;
constexpr uint32_t MAX_PAYLOAD_SIZE = 10 * 1024 * 1024;  // 10 MB

inline uint32_t crc32(const uint8_t* data, size_t len) {
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            }
            t[i] = c;
        }
        return t;
    }();

    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

// Frame: [magic 4B][version 1B][length 4B][payload][crc32 4B]
// Header is 9 bytes (was 8 before version was added).
std::vector<uint8_t> encodePacket(const std::string& payload) {
    std::vector<uint8_t> packet;

    uint32_t magic_net = htonl(MAGIC_NUMBER);
    const uint8_t* magic_bytes = reinterpret_cast<const uint8_t*>(&magic_net);
    packet.insert(packet.end(), magic_bytes, magic_bytes + 4);

    packet.push_back(PROTOCOL_VERSION);

    uint32_t len_net = htonl(static_cast<uint32_t>(payload.size()));
    const uint8_t* len_bytes = reinterpret_cast<const uint8_t*>(&len_net);
    packet.insert(packet.end(), len_bytes, len_bytes + 4);

    packet.insert(packet.end(), payload.begin(), payload.end());

    uint32_t crc = crc32(reinterpret_cast<const uint8_t*>(payload.data()), payload.size());
    uint32_t crc_net = htonl(crc);
    const uint8_t* crc_bytes = reinterpret_cast<const uint8_t*>(&crc_net);
    packet.insert(packet.end(), crc_bytes, crc_bytes + 4);

    return packet;
}

struct DecodedPacket {
    bool valid;
    std::string payload;
};

DecodedPacket decodePacket(const uint8_t* data, size_t data_len) {
    // header is now magic(4) + version(1) + length(4) = 9 bytes minimum
    if (data_len < 9) {
        return {false, ""};
    }

    uint32_t magic_net;
    std::memcpy(&magic_net, data, 4);
    uint32_t magic = ntohl(magic_net);
    if (magic != MAGIC_NUMBER) {
        return {false, ""};
    }

    uint8_t version = data[4];
    if (version != PROTOCOL_VERSION) {
        return {false, ""};  // unsupported/future protocol version
    }

    uint32_t len_net;
    std::memcpy(&len_net, data + 5, 4);
    uint32_t length = ntohl(len_net);

    if (length > MAX_PAYLOAD_SIZE) {
        return {false, ""};
    }

    if (data_len < 9 + static_cast<size_t>(length) + 4) {
        return {false, ""};
    }

    const uint8_t* payload_start = data + 9;
    std::string payload(reinterpret_cast<const char*>(payload_start), length);

    uint32_t stored_crc_net;
    std::memcpy(&stored_crc_net, data + 9 + length, 4);
    uint32_t stored_crc = ntohl(stored_crc_net);

    uint32_t computed_crc = crc32(payload_start, length);
    if (computed_crc != stored_crc) {
        return {false, ""};
    }

    return {true, payload};
}