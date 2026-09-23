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

    Message m1; m1.key = "device-001"; m1.payload = "reading 1";
    Message m2; m2.key = "device-001"; m2.payload = "reading 2";
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