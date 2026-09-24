#include <gtest/gtest.h>
#include "protocol.h"

TEST(ProtocolTest, RoundTripSimplePayload) {
    std::string original = "PRODUCE hello world";
    auto packet = encodePacket(original);

    DecodedPacket decoded = decodePacket(packet.data(), packet.size());

    EXPECT_TRUE(decoded.valid);
    EXPECT_EQ(decoded.payload, original);
}

TEST(ProtocolTest, IncompleteDataIsRejected) {
    std::string original = "PRODUCE hello world";
    auto packet = encodePacket(original);

    size_t partial_size = packet.size() / 2;
    DecodedPacket decoded = decodePacket(packet.data(), partial_size);

    EXPECT_FALSE(decoded.valid);
}

TEST(ProtocolTest, OversizedLengthIsRejected) {
    std::vector<uint8_t> packet;
    uint32_t magic_net = htonl(MAGIC_NUMBER);
    uint32_t huge_len_net = htonl(50 * 1024 * 1024); // 50 MB — over our 10 MB cap

    const uint8_t* mb = reinterpret_cast<const uint8_t*>(&magic_net);
    packet.insert(packet.end(), mb, mb + 4);
    const uint8_t* lb = reinterpret_cast<const uint8_t*>(&huge_len_net);
    packet.insert(packet.end(), lb, lb + 4);

    DecodedPacket decoded = decodePacket(packet.data(), packet.size());
    EXPECT_FALSE(decoded.valid);
}