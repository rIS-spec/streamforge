#pragma once
#include <string>
#include <cstdint>
#include <atomic>

class Partitioner {
  public:
    explicit Partitioner(size_t partition_count) : partition_count_(partition_count) {}

    size_t getPartition(const std::string& key) {
        if (key.empty()) {
            return round_robin_.fetch_add(1) % partition_count_;
        }
        std::hash<std::string> hasher;
        return hasher(key) % partition_count_;
    }

  private:
    size_t partition_count_;
    std::atomic<size_t> round_robin_{0};
};