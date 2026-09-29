#pragma once
#include <fcntl.h>
#include <unistd.h>
#include <fstream>
#include <string>
#include <mutex>
#include <vector>
#include <stdexcept>
#include "message.h"
#include "protocol.h"

class Log {
  public:
    explicit Log(const std::string& filepath) {
        fd_ = open(filepath.c_str(), O_APPEND | O_CREAT | O_WRONLY, 0644);
        if (fd_ < 0)
            throw std::runtime_error("Log: failed to open " + filepath);
    }

    ~Log() {
        if (fd_ >= 0)
            close(fd_);
    }

    // non-copyable (owns a raw fd), non-movable for now (keeps it simple, matches Topic's
    // mutex-owning style)
    Log(const Log&) = delete;
    Log& operator=(const Log&) = delete;

    void append(const Message& msg) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto packet = encodePacket(msg.payload);

        size_t written = 0;
        while (written < packet.size()) {
            ssize_t n = write(fd_, packet.data() + written, packet.size() - written);
            if (n < 0)
                throw std::runtime_error("Log: write() failed");
            written += static_cast<size_t>(n);
        }

        if (fsync(fd_) != 0)
            throw std::runtime_error("Log: fsync() failed");
    }

    std::vector<Message> readAll(const std::string& filepath) {
        std::vector<Message> result;
        std::ifstream in(filepath, std::ios::binary);
        if (!in)
            return result;

        std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)),
                                  std::istreambuf_iterator<char>());

        size_t offset = 0;
        while (offset < data.size()) {
            DecodedPacket decoded = decodePacket(data.data() + offset, data.size() - offset);
            if (!decoded.valid)
                break;

            Message m;
            m.payload = decoded.payload;
            result.push_back(m);

            offset += 9 + decoded.payload.size() + 4;
        }

        return result;
    }

  private:
    int fd_ = -1;
    std::mutex mutex_;
};