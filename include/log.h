#pragma once
#include <fstream>
#include <string>
#include <mutex>
#include <vector>
#include "message.h"
#include "protocol.h"

class Log {
public:
    explicit Log(const std::string& filepath)
        : file_(filepath, std::ios::app | std::ios::binary) {
    }

    void append(const Message& msg) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto packet = encodePacket(msg.payload);
        file_.write(reinterpret_cast<const char*>(packet.data()), packet.size());
        file_.flush();
    }

    std::vector<Message> readAll(const std::string& filepath) {
        std::vector<Message> result;
        std::ifstream in(filepath, std::ios::binary);
        if (!in) return result;

        std::vector<uint8_t> data(
            (std::istreambuf_iterator<char>(in)),
            std::istreambuf_iterator<char>()
        );

        size_t offset = 0;
        while (offset < data.size()) {
            DecodedPacket decoded = decodePacket(data.data() + offset, data.size() - offset);
            if (!decoded.valid) break;

            Message m;
            m.payload = decoded.payload;
            result.push_back(m);

            offset += 8 + decoded.payload.size();
        }

        return result;
    }

private:
    std::ofstream file_;
    std::mutex mutex_;
};