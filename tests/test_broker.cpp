#include <gtest/gtest.h>
#include <cstdio>
#include "broker.h"

void cleanupLogs(const std::string& prefix, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        std::remove((prefix + "_p" + std::to_string(i) + ".log").c_str());
    }
}

TEST(BrokerTest, SameKeyAlwaysGoesToSamePartition) {
    cleanupLogs("test_broker_key", 4);
    Broker broker(4, "test_broker_key");

    Message m1;
    m1.key = "device-001";
    m1.payload = "reading 1";
    Message m2;
    m2.key = "device-001";
    m2.payload = "reading 2";
    broker.publish(m1);
    broker.publish(m2);

    int found_partition = -1;
    int total_found = 0;

    for (size_t p = 0; p < broker.partitionCount(); ++p) {
        Message out;
        while (broker.consume(p, out)) {
            if (found_partition == -1) {
                found_partition = static_cast<int>(p);
            } else {
                EXPECT_EQ(found_partition, static_cast<int>(p));
            }
            ++total_found;
        }
    }

    EXPECT_EQ(total_found, 2);
}

TEST(BrokerTest, ConsumeOnInvalidPartitionReturnsFalse) {
    cleanupLogs("test_broker_invalid", 4);
    Broker broker(4, "test_broker_invalid");

    Message out;
    EXPECT_FALSE(broker.consume(99, out));  // way out of range
    EXPECT_FALSE(broker.consume(4, out));   // exactly one past the last valid index (0-3)
}

TEST(BrokerTest, PublishRejectedWhenQueueFull) {
    cleanupLogs("test_broker_full", 1);
    Broker broker(1, "test_broker_full", 2);

    Message m;
    m.payload = "x";
    EXPECT_TRUE(broker.publish(m));
    EXPECT_TRUE(broker.publish(m));
    EXPECT_FALSE(broker.publish(m)) << "third publish must be rejected: queue cap is 2";

    Message out;
    ASSERT_TRUE(broker.consume(0, out));
    EXPECT_TRUE(broker.publish(m)) << "capacity freed by consume, publish must succeed again";

    // The rejected message must never have reached the disk: 3 accepted writes, not 4.
    Broker reopened(1, "test_broker_full");
    int count = 0;
    while (reopened.consume(0, out)) ++count;
    EXPECT_EQ(count, 3);
}
