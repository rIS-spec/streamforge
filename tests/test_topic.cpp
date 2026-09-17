#include <gtest/gtest.h>
#include "topic.h"

TEST(TopicTest, ConsumesInFIFOOrder) {
    Topic topic;
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
    Topic topic;
    Message out;
    EXPECT_FALSE(topic.consume(out));
}