#include <gtest/gtest.h>
#include <cstdio>
#include "topic.h"

TEST(TopicTest, ConsumesInFIFOOrder) {
    std::remove("test_topic.log");
    Topic topic("test_topic.log");
    Message m1; m1.payload = "first";
    Message m2; m2.payload = "second";
    topic.publish(m1);
    topic.publish(m2);

    Message out;
    ASSERT_TRUE(topic.consume(out));
    EXPECT_EQ(out.payload, "first");

    ASSERT_TRUE(topic.consume(out));
    EXPECT_EQ(out.payload, "second");
}

TEST(TopicTest, ConsumeOnEmptyQueueReturnsFalse) {
    std::remove("test_topic.log");
    Topic topic("test_topic.log");
    Message out;
    EXPECT_FALSE(topic.consume(out));
}