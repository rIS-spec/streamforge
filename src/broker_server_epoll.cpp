#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <fcntl.h>
#include <cerrno>
#include "message.h"
#include "broker.h"
#include "thread_pool.h"
#include "protocol.h"
#include <csignal>
#include <atomic>
#include "logger.h"
#include <atomic>
#include <chrono>

std::atomic<uint64_t> total_produced{0};
std::atomic<uint64_t> total_consumed{0};
std::atomic<uint64_t> total_connections{0};

constexpr size_t MAX_CLIENT_BUFFER_SIZE =
    9 + MAX_PAYLOAD_SIZE + 4;  // largest possible single frame
std::atomic<int> active_connections{0};

auto server_start_time = std::chrono::steady_clock::now();

std::atomic<bool> shutdown_requested{false};

void signalHandler(int) {
    shutdown_requested = true;
}

std::mutex buffers_mutex;
std::unordered_map<int, std::vector<uint8_t>> client_buffers;
std::unordered_map<int, std::chrono::steady_clock::time_point>
    client_last_activity;  // guarded by buffers_mutex

void setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void closeClient(int client_fd, int epoll_fd) {
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr);
    close(client_fd);
    std::lock_guard<std::mutex> lock(buffers_mutex);
    client_buffers.erase(client_fd);
    client_last_activity.erase(client_fd);
    active_connections.fetch_sub(1, std::memory_order_relaxed);
    logEvent(LogLevel::DEBUG, "client_disconnected", {{"fd", std::to_string(client_fd)}});
}

void rearm(int client_fd, int epoll_fd) {
    epoll_event ev{};
    ev.events = EPOLLIN | EPOLLONESHOT;
    ev.data.fd = client_fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_MOD, client_fd, &ev);
}

void processClientData(int client_fd, Broker& broker, int epoll_fd) {
    char temp[4096];
    while (true) {
        ssize_t n = recv(client_fd, temp, sizeof(temp), 0);
        if (n > 0) {
            bool buffer_exceeded = false;
            size_t buffer_size = 0;
            {
                std::lock_guard<std::mutex> lock(buffers_mutex);
                auto it = client_buffers.find(client_fd);
                if (it == client_buffers.end())
                    return;
                client_last_activity[client_fd] = std::chrono::steady_clock::now();
                it->second.insert(it->second.end(), temp, temp + n);

                if (it->second.size() > MAX_CLIENT_BUFFER_SIZE) {
                    buffer_exceeded = true;
                    buffer_size = it->second.size();
                }
            }  // lock released before closeClient(), which locks the same mutex

            if (buffer_exceeded) {
                logEvent(
                    LogLevel::WARN, "client_buffer_exceeded_limit",
                    {{"fd", std::to_string(client_fd)}, {"size", std::to_string(buffer_size)}});
                closeClient(client_fd, epoll_fd);
                return;
            }
        } else if (n == 0) {
            closeClient(client_fd, epoll_fd);  // peer closed the connection
            return;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;  // drained: go decode
            closeClient(client_fd, epoll_fd);
            return;
        }
    }

    while (true) {
        std::vector<uint8_t> snapshot;
        {
            std::lock_guard<std::mutex> lock(buffers_mutex);
            auto it = client_buffers.find(client_fd);
            if (it == client_buffers.end())
                return;
            snapshot = it->second;
        }
        DecodedPacket decoded = decodePacket(snapshot.data(), snapshot.size());
        if (!decoded.valid)
            break;

        std::string response;
        if (decoded.payload.starts_with("PRODUCE ")) {
            Message m;
            m.payload = decoded.payload.substr(8);
            try {
                try {
                    broker.publish(m);
                    total_produced.fetch_add(1, std::memory_order_relaxed);
                    response = "OK";
                } catch (const std::exception& e) {
                    logEvent(LogLevel::ERROR, "publish_failed",
                             {{"fd", std::to_string(client_fd)}, {"error", e.what()}});
                    response = "ERR publish failed";
                }
            } catch (const std::exception& e) {
                logEvent(LogLevel::ERROR, "publish_failed",
                         {{"fd", std::to_string(client_fd)}, {"error", e.what()}});
                response = "ERR publish failed";
            }
        } else if (decoded.payload.starts_with("CONSUME ")) {
            size_t idx = std::stoul(decoded.payload.substr(8));
            Message out;
            if (broker.consume(idx, out)) {
                total_consumed.fetch_add(1, std::memory_order_relaxed);
                response = "MSG: " + out.payload;
            } else {
                response = "EMPTY";
            }
        } else if (decoded.payload == "HEALTH") {
            response = "HEALTHY";
        } else if (decoded.payload == "METRICS") {
            auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
                              std::chrono::steady_clock::now() - server_start_time)
                              .count();
            response = "{\"messages_produced\":" + std::to_string(total_produced.load()) +
                       ",\"messages_consumed\":" + std::to_string(total_consumed.load()) +
                       ",\"connections_total\":" + std::to_string(total_connections.load()) +
                       ",\"uptime_seconds\":" + std::to_string(uptime) + "}";
        } else {
            response = "ERR unknown command";
        }

        auto reply = encodePacket(response);
        send(client_fd, reply.data(), reply.size(), 0);

        size_t consumed = 9 + decoded.payload.size() + 4;
        std::lock_guard<std::mutex> lock(buffers_mutex);
        auto it = client_buffers.find(client_fd);
        if (it == client_buffers.end())
            return;
        it->second.erase(it->second.begin(), it->second.begin() + consumed);
    }

    rearm(client_fd, epoll_fd);
}

struct Config {
    int port = 9001;
    int workers = 4;
    int partitions = 4;
    std::string data_dir = ".";
    int max_connections = 1000;
    int idle_timeout_seconds = 60;
};

Config parseArgs(int argc, char* argv[]) {
    Config cfg;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc)
            cfg.port = std::stoi(argv[++i]);
        else if (arg == "--workers" && i + 1 < argc)
            cfg.workers = std::stoi(argv[++i]);
        else if (arg == "--partitions" && i + 1 < argc)
            cfg.partitions = std::stoi(argv[++i]);
        else if (arg == "--data-dir" && i + 1 < argc)
            cfg.data_dir = argv[++i];
        else if (arg == "--max-connections" && i + 1 < argc)
            cfg.max_connections = std::stoi(argv[++i]);
        else if (arg == "--idle-timeout" && i + 1 < argc)
            cfg.idle_timeout_seconds = std::stoi(argv[++i]);
    }
    return cfg;
}

int main(int argc, char* argv[]) {
    Config cfg = parseArgs(argc, argv);

    Broker broker(cfg.partitions, cfg.data_dir + "/streamforge");
    auto pool = std::make_unique<ThreadPool>(cfg.workers);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setNonBlocking(server_fd);

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(cfg.port);
    if (bind(server_fd, (sockaddr*)&address, sizeof(address)) < 0) {
        logEvent(LogLevel::ERROR, "bind_failed",
                 {{"port", std::to_string(cfg.port)}, {"errno", strerror(errno)}});
        return 1;
    }
    if (listen(server_fd, 128) < 0) {
        logEvent(LogLevel::ERROR, "listen_failed", {{"errno", strerror(errno)}});
        return 1;
    }

    int epoll_fd = epoll_create1(0);
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = server_fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev);

    logEvent(LogLevel::INFO, "broker_started",
             {{"port", std::to_string(cfg.port)},
              {"workers", std::to_string(cfg.workers)},
              {"partitions", std::to_string(cfg.partitions)},
              {"data_dir", cfg.data_dir},
              {"max_connections", std::to_string(cfg.max_connections)},
              {"idle_timeout_seconds", std::to_string(cfg.idle_timeout_seconds)}});

    const int MAX_EVENTS = 64;
    epoll_event events[MAX_EVENTS];

    while (!shutdown_requested) {
        int n = epoll_wait(epoll_fd, events, MAX_EVENTS, 500);
        if (n < 0)
            break;
        {
            auto now = std::chrono::steady_clock::now();
            std::vector<int> idle_fds;
            {
                std::lock_guard<std::mutex> lock(buffers_mutex);
                for (const auto& [fd, last_active] : client_last_activity) {
                    auto idle_seconds =
                        std::chrono::duration_cast<std::chrono::seconds>(now - last_active).count();
                    if (idle_seconds >= cfg.idle_timeout_seconds)
                        idle_fds.push_back(fd);
                }
            }
            for (int fd : idle_fds) {
                logEvent(LogLevel::WARN, "client_idle_timeout",
                         {{"fd", std::to_string(fd)},
                          {"idle_timeout_seconds", std::to_string(cfg.idle_timeout_seconds)}});
                closeClient(fd, epoll_fd);
            }
        }

        for (int i = 0; i < n; ++i) {
            int fd = events[i].data.fd;
            if (fd == server_fd) {
                while (true) {
                    sockaddr_in client_addr{};
                    socklen_t len = sizeof(client_addr);
                    int client_fd = accept(server_fd, (sockaddr*)&client_addr, &len);
                    if (client_fd < 0)
                        break;

                    if (active_connections.load(std::memory_order_relaxed) >= cfg.max_connections) {
                        logEvent(LogLevel::WARN, "connection_rejected_max_reached",
                                 {{"max_connections", std::to_string(cfg.max_connections)}});
                        auto rejection = encodePacket("ERR max connections reached");
                        send(client_fd, rejection.data(), rejection.size(), 0);
                        close(client_fd);
                        continue;
                    }

                    setNonBlocking(client_fd);
                    epoll_event cev{};
                    cev.events = EPOLLIN | EPOLLONESHOT;
                    cev.data.fd = client_fd;
                    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &cev);
                    std::lock_guard<std::mutex> lock(buffers_mutex);
                    client_buffers[client_fd] = {};
                    client_last_activity[client_fd] = std::chrono::steady_clock::now();
                    active_connections.fetch_add(1, std::memory_order_relaxed);
                    total_connections.fetch_add(1, std::memory_order_relaxed);
                }
            } else {
                pool->submit(
                    [fd, &broker, epoll_fd]() { processClientData(fd, broker, epoll_fd); });
            }
        }
    }
    logEvent(LogLevel::INFO, "shutdown_signal_received", {});
    pool.reset();  // join workers first: they still use epoll_fd until they finish
    close(server_fd);
    close(epoll_fd);
    return 0;
}