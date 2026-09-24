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

std::mutex buffers_mutex;
std::unordered_map<int, std::vector<uint8_t>> client_buffers;

void setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void closeClient(int client_fd, int epoll_fd) {
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr);
    close(client_fd);
    std::lock_guard<std::mutex> lock(buffers_mutex);
    client_buffers.erase(client_fd);
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
            std::lock_guard<std::mutex> lock(buffers_mutex);
            auto it = client_buffers.find(client_fd);
            if (it == client_buffers.end()) return;
            it->second.insert(it->second.end(), temp, temp + n);
        } else if (n == 0) {
            closeClient(client_fd, epoll_fd);
            return;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            closeClient(client_fd, epoll_fd);
            return;
        }
    }

    while (true) {
        std::vector<uint8_t> snapshot;
        {
            std::lock_guard<std::mutex> lock(buffers_mutex);
            auto it = client_buffers.find(client_fd);
            if (it == client_buffers.end()) return;
            snapshot = it->second;
        }
        DecodedPacket decoded = decodePacket(snapshot.data(), snapshot.size());
        if (!decoded.valid) break;

        std::string response;
        if (decoded.payload.starts_with("PRODUCE ")) {
            Message m;
            m.payload = decoded.payload.substr(8);
            broker.publish(m);
            response = "OK";
        } else if (decoded.payload.starts_with("CONSUME ")) {
            size_t idx = std::stoul(decoded.payload.substr(8));
            Message out;
            response = broker.consume(idx, out) ? ("MSG: " + out.payload) : "EMPTY";
        } else {
            response = "ERR unknown command";
        }

        auto reply = encodePacket(response);
        send(client_fd, reply.data(), reply.size(), 0);

        size_t consumed = 8 + decoded.payload.size();
        std::lock_guard<std::mutex> lock(buffers_mutex);
        auto it = client_buffers.find(client_fd);
        if (it == client_buffers.end()) return;
        it->second.erase(it->second.begin(), it->second.begin() + consumed);
    }

    rearm(client_fd, epoll_fd);
}

int main() {
    Broker broker(4, "streamforge");
    ThreadPool pool(4);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setNonBlocking(server_fd);

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(9001);
    bind(server_fd, (sockaddr*)&address, sizeof(address));
    listen(server_fd, 128);

    int epoll_fd = epoll_create1(0);
    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = server_fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev);

    std::cout << "StreamForge (epoll) listening on port 9001\n";

    const int MAX_EVENTS = 64;
    epoll_event events[MAX_EVENTS];

    while (true) {
        int n = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        for (int i = 0; i < n; ++i) {
            int fd = events[i].data.fd;
            if (fd == server_fd) {
                while (true) {
                    sockaddr_in client_addr{};
                    socklen_t len = sizeof(client_addr);
                    int client_fd = accept(server_fd, (sockaddr*)&client_addr, &len);
                    if (client_fd < 0) break;
                    setNonBlocking(client_fd);
                    epoll_event cev{};
                    cev.events = EPOLLIN | EPOLLONESHOT;
                    cev.data.fd = client_fd;
                    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &cev);
                    std::lock_guard<std::mutex> lock(buffers_mutex);
                    client_buffers[client_fd] = {};
                }
            } else {
                pool.submit([fd, &broker, epoll_fd]() {
                    processClientData(fd, broker, epoll_fd);
                });
            }
        }
    }
    close(server_fd);
    close(epoll_fd);
    return 0;
}