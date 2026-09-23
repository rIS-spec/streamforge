#include <gtest/gtest.h>
#include <cstdio>
#include "thread_pool.h"
#include "topic.h"

TEST(ThreadPoolTest, ProcessesAllSubmittedTasks) {
    std::remove("test_thread_pool.log");
    Topic topic("test_thread_pool.log");
    {
        ThreadPool pool(4);
        for (int i = 0; i < 1000; ++i) {
            pool.submit([&topic] {
                Message m;
                m.payload = "msg";
                topic.publish(m);
            });
        }
    }

    int count = 0;
    Message out;
    while (topic.consume(out)) {
        ++count;
    }

    EXPECT_EQ(count, 1000);
}