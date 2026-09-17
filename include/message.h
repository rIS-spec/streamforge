#pragma once
#include <string>
#include <cstdint>
#include <chrono>


struct Message {
    std::string topic;
    uint32_t partition;
    uint64_t offset;
    int64_t timestamp;
    std::string producer_id;
    std::string key;
    std::string payload;
};