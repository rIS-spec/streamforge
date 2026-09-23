#pragma once
#include <queue>
#include <mutex>
#include "message.h"
#include "log.h"

class Topic {
public:
    explicit Topic(const std::string& log_path) : log_(log_path) {
        for (const auto& m : log_.readAll(log_path)) {
            messages_.push(m);
        }
    }

    void publish(const Message& msg) {
        std::lock_guard<std::mutex> lock(mutex_);
        log_.append(msg);
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
    Log log_;
};