#pragma once
#include <queue>
#include <mutex>
#include "message.h"
#include "log.h"

class Topic {
  public:
    explicit Topic(const std::string& log_path, size_t max_queue_size = 100000)
        : log_(log_path), max_queue_size_(max_queue_size) {
        for (const auto& m : log_.readAll(log_path)) {
            messages_.push(m);
        }
    }

    bool publish(const Message& msg) {
        std::lock_guard<std::mutex> lock(mutex_);
        // Backpressure: refuse BEFORE paying for fsync on a message we will not accept.
        if (messages_.size() >= max_queue_size_)
            return false;
        log_.append(msg);
        messages_.push(msg);
        return true;
    }

    bool consume(Message& out) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (messages_.empty())
            return false;
        out = messages_.front();
        messages_.pop();
        return true;
    }

  private:
    std::queue<Message> messages_;
    std::mutex mutex_;
    Log log_;
    size_t max_queue_size_;
};