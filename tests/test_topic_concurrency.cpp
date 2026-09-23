#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <cstdio>
#include "topic.h"

TEST(TopicConcurrencyTest, HandlesManyProducersSafely) {
    std::remove("test_topic_concurrency.log");
    Topic topic("test_topic_concurrency.log");
    const int num_threads = 10;
    const int messages_per_thread = 1000;
    std::vector<std::thread> threads;

    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&topic, messages_per_thread]() {
            for (int j = 0; j < messages_per_thread; ++j) {
                Message m;
                m.payload = "msg";
                topic.publish(m);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    int count = 0;
    Message out;
    while (topic.consume(out)) {
        ++count;
    }

    EXPECT_EQ(count, num_threads * messages_per_thread);
}