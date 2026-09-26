#pragma once
#include <string>
#include <cstdint>
#include <cstring>
#include <vector>
#include <array>
#include <arpa/inet.h>

constexpr uint32_t MAGIC_NUMBER = 0x53465247; // "SFRG" in hex
constexpr uint32_t MAX_PAYLOAD_SIZE = 10 * 1024 * 1024; // 10 MB

// Small table-based CRC32 (standard IEEE 802.3 polynomial, 0xEDB88320),
// no external dependency. Table is built once, on first use.
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

// Frame: [magic 4B][length 4B][payload][crc32 4B]
// crc32 is computed over the payload only.
std::vector<uint8_t> encodePacket(const std::string& payload) {
    std::vector<uint8_t> packet;

    uint32_t magic_net = htonl(MAGIC_NUMBER);
    uint32_t len_net = htonl(static_cast<uint32_t>(payload.size()));

    const uint8_t* magic_bytes = reinterpret_cast<const uint8_t*>(&magic_net);
    packet.insert(packet.end(), magic_bytes, magic_bytes + 4);

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
    if (data_len < 8) {
        return {false, ""};
    }

    uint32_t magic_net, len_net;
    std::memcpy(&magic_net, data, 4);
    std::memcpy(&len_net, data + 4, 4);

    uint32_t magic = ntohl(magic_net);
    uint32_t length = ntohl(len_net);

    if (length > MAX_PAYLOAD_SIZE) {
        return {false, ""};
    }

    if (magic != MAGIC_NUMBER) {
        return {false, ""};
    }

    // now need header(8) + payload(length) + crc(4)
    if (data_len < 8 + static_cast<size_t>(length) + 4) {
        return {false, ""};
    }

    const uint8_t* payload_start = data + 8;
    std::string payload(reinterpret_cast<const char*>(payload_start), length);

    uint32_t stored_crc_net;
    std::memcpy(&stored_crc_net, data + 8 + length, 4);
    uint32_t stored_crc = ntohl(stored_crc_net);

    uint32_t computed_crc = crc32(payload_start, length);
    if (computed_crc != stored_crc) {
        return {false, ""}; // bit-flip / corruption detected
    }

    return {true, payload};
}