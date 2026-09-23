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