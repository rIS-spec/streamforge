#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <algorithm>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "protocol.h"

using Clock = std::chrono::steady_clock;

struct ClientResult {
    std::vector<double> latencies_ms;  // one entry per successful round trip
    size_t bytes_sent = 0;
};

// One simulated client: opens its own socket, sends `count` PRODUCE requests
// of `payload_size` bytes each, waits for each response before sending the
// next (closed-loop load, matches how a real synchronous producer behaves).
ClientResult runClient(const std::string& host, int port, int count, size_t payload_size) {
    ClientResult result;

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, host.c_str(), &addr.sin_addr);

    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "connect() failed\n";
        return result;
    }

    std::string body = "PRODUCE " + std::string(payload_size, 'x');
    uint8_t buffer[65536];

    for (int i = 0; i < count; ++i) {
        auto packet = encodePacket(body);

        auto t0 = Clock::now();

        size_t sent = 0;
        while (sent < packet.size()) {
            ssize_t n = send(sock, packet.data() + sent, packet.size() - sent, 0);
            if (n <= 0) {
                close(sock);
                return result;
            }
            sent += static_cast<size_t>(n);
        }

        ssize_t bytes_read = recv(sock, buffer, sizeof(buffer), 0);
        auto t1 = Clock::now();

        if (bytes_read <= 0)
            break;  // server closed or error — stop this client

        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        result.latencies_ms.push_back(ms);
        result.bytes_sent += packet.size();
    }

    close(sock);
    return result;
}

double percentile(std::vector<double>& sorted_latencies, double p) {
    if (sorted_latencies.empty())
        return 0.0;
    size_t idx = static_cast<size_t>(p / 100.0 * (sorted_latencies.size() - 1));
    return sorted_latencies[idx];
}

int main(int argc, char* argv[]) {
    std::string host = "127.0.0.1";
    int port = 9001;
    int concurrency = 10;
    int messages_per_client = 100;
    size_t payload_size = 100;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--concurrency" && i + 1 < argc)
            concurrency = std::stoi(argv[++i]);
        else if (arg == "--messages" && i + 1 < argc)
            messages_per_client = std::stoi(argv[++i]);
        else if (arg == "--payload-size" && i + 1 < argc)
            payload_size = std::stoul(argv[++i]);
        else if (arg == "--port" && i + 1 < argc)
            port = std::stoi(argv[++i]);
    }

    std::cout << "Benchmark: concurrency=" << concurrency
              << " messages_per_client=" << messages_per_client << " payload_size=" << payload_size
              << "B\n";

    std::vector<std::thread> threads;
    std::vector<ClientResult> results(concurrency);

    auto wall_start = Clock::now();

    for (int i = 0; i < concurrency; ++i) {
        threads.emplace_back(
            [&, i]() { results[i] = runClient(host, port, messages_per_client, payload_size); });
    }
    for (auto& t : threads) t.join();

    auto wall_end = Clock::now();
    double wall_seconds = std::chrono::duration<double>(wall_end - wall_start).count();

    std::vector<double> all_latencies;
    size_t total_messages = 0;
    size_t total_bytes = 0;
    for (auto& r : results) {
        all_latencies.insert(all_latencies.end(), r.latencies_ms.begin(), r.latencies_ms.end());
        total_messages += r.latencies_ms.size();
        total_bytes += r.bytes_sent;
    }
    std::sort(all_latencies.begin(), all_latencies.end());

    double throughput_msg_s = total_messages / wall_seconds;
    double throughput_mb_s = (total_bytes / 1024.0 / 1024.0) / wall_seconds;

    std::cout << "\n--- Results ---\n";
    std::cout << "Completed messages: " << total_messages << " / "
              << (concurrency * messages_per_client) << " requested\n";
    std::cout << "Wall time: " << wall_seconds << " s\n";
    std::cout << "Throughput: " << throughput_msg_s << " msg/s, " << throughput_mb_s << " MB/s\n";
    std::cout << "Latency p50: " << percentile(all_latencies, 50) << " ms\n";
    std::cout << "Latency p95: " << percentile(all_latencies, 95) << " ms\n";
    std::cout << "Latency p99: " << percentile(all_latencies, 99) << " ms\n";

    return 0;
}