#pragma once
#include <queue>
#include "message.h"

class Topic {
public:

    void publish(const Message& msg) {
        messages_.push(msg);
    }

    bool consume(Message& out) {
        if (messages_.empty()) return false;
        out = messages_.front();
        messages_.pop();
        return true;
    }

private:
    std::queue<Message> messages_;
};