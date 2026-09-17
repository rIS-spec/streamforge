#include <gtest/gtest.h>
#include "message.h"

TEST(MessageTest, FieldsStoreCorrectly) {
    Message m;
    m.topic = "telemetry";
    m.partition = 2;
    m.offset = 100;
    m.payload = "hello";

    EXPECT_EQ(m.topic, "telemetry");
    EXPECT_EQ(m.partition, 2);
    EXPECT_EQ(m.offset, 100);
    EXPECT_EQ(m.payload, "hello");
}