#pragma once
#include <queue>
#include <mutex>
#include "message.h"

class Topic {
public:
    void publish(const Message& msg) {
        std::lock_guard<std::mutex> lock(mutex_);
        messages_.push(msg);
    }

    bool consume(Message& out) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (messages_.empty()) return false;
        out = messages_.front();
        messages_.pop();
        return true;
    }

private:
    std::queue<Message> messages_;
    std::mutex mutex_;
};