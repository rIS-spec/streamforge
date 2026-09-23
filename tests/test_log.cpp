#include <gtest/gtest.h>
#include <cstdio>
#include "log.h"

TEST(LogTest, AppendAndReadBack) {
    const std::string path = "test_log_output.bin";
    std::remove(path.c_str());

    {
        Log log(path);
        Message m1; m1.payload = "first message";
        Message m2; m2.payload = "second message";
        log.append(m1);
        log.append(m2);
    }

    Log log2(path);
    auto messages = log2.readAll(path);

    ASSERT_EQ(messages.size(), 2);
    EXPECT_EQ(messages[0].payload, "first message");
    EXPECT_EQ(messages[1].payload, "second message");

    std::remove(path.c_str());
}