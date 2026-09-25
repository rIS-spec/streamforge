#include <gtest/gtest.h>
#include <cstdio>
#include <fstream>
#include "log.h"
#include "topic.h"

// Scenario 1: clean shutdown + restart — log survives, replays in order
TEST(RecoveryTest, NormalRestartPreservesMessages) {
    const std::string path = "recovery_normal.bin";
    std::remove(path.c_str());

    {
        Topic t(path);
        Message m1; m1.payload = "alpha";
        Message m2; m2.payload = "beta";
        t.publish(m1);
        t.publish(m2);
    } // Topic + Log destructors run here — simulates a clean process exit

    Topic restarted(path);
    Message out;
    ASSERT_TRUE(restarted.consume(out));
    EXPECT_EQ(out.payload, "alpha");
    ASSERT_TRUE(restarted.consume(out));
    EXPECT_EQ(out.payload, "beta");

    std::remove(path.c_str());
}

// Scenario 2: "kill" — no destructor runs, fd is closed by the OS on process exit
TEST(RecoveryTest, DataSurvivesWithoutCleanClose) {
    const std::string path = "recovery_kill.bin";
    std::remove(path.c_str());

    {
        Log log(path);
        Message m; m.payload = "not-cleanly-closed";
        log.append(m); // fsync() already happened by the time this returns
        // deliberately no explicit close/flush beyond what append() already did
    }

    Log log2(path);
    auto messages = log2.readAll(path);
    ASSERT_EQ(messages.size(), 1);
    EXPECT_EQ(messages[0].payload, "not-cleanly-closed");

    std::remove(path.c_str());
}

// Scenario 3: simulated corrupt/incomplete tail record.
TEST(RecoveryTest, TruncatedTailRecordIsIgnoredNotCrashed) {
    const std::string path = "recovery_corrupt.bin";
    std::remove(path.c_str());

    {
        Log log(path);
        Message m; m.payload = "complete-message";
        log.append(m);
    }

    // manually append a truncated/garbage tail directly to the file,
    // bypassing Log entirely — this is what a mid-write crash would leave behind
    {
        std::ofstream raw(path, std::ios::app | std::ios::binary);
        uint8_t junk[5] = {0x53, 0x46, 0x52, 0x47, 0x00}; // starts like valid magic, then nothing
        raw.write(reinterpret_cast<const char*>(junk), sizeof(junk));
    }

    Log log2(path);
    auto messages = log2.readAll(path);

    ASSERT_EQ(messages.size(), 1) << "corrupt/truncated tail must not be treated as a valid message";
    EXPECT_EQ(messages[0].payload, "complete-message");

    std::remove(path.c_str());
}