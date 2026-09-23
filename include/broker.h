#pragma once
#include <vector>
#include <memory>
#include "topic.h"
#include "partitioner.h"

class Broker {
public:
    Broker(size_t partition_count, const std::string& log_prefix)
        : partitioner_(partition_count) {
        for (size_t i = 0; i < partition_count; ++i) {
            std::string path = log_prefix + "_p" + std::to_string(i) + ".log";
            partitions_.push_back(std::make_unique<Topic>(path));
        }
    }

    void publish(const Message& msg) {
        size_t idx = partitioner_.getPartition(msg.key);
        partitions_[idx]->publish(msg);
    }

    bool consume(size_t partition_idx, Message& out) {
        if (partition_idx >= partitions_.size()) return false;
        return partitions_[partition_idx]->consume(out);
    }

    size_t partitionCount() const {
        return partitions_.size();
    }

private:
    std::vector<std::unique_ptr<Topic>> partitions_;
    Partitioner partitioner_;
};