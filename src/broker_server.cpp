#include <iostream>
#include <string>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include "message.h"
#include "topic.h"
#include "thread_pool.h"

void handleClient(int client_fd, Topic& topic) {
    char buffer[1024];
    while (true) {
        memset(buffer, 0, sizeof(buffer));
        ssize_t bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        if (bytes_read <= 0) break;

        std::string line(buffer);
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
            line.pop_back();
        }

        std::string response;
        if (line.starts_with("PRODUCE ")) {
            Message m;
            m.payload = line.substr(8);
            topic.publish(m);
            response = "OK\n";
        } else if (line == "CONSUME") {
            Message out;
            if (topic.consume(out)) {
                response = "MSG: " + out.payload + "\n";
            } else {
                response = "EMPTY\n";
            }
        } else {
            response = "ERR unknown command\n";
        }

        send(client_fd, response.c_str(), response.size(), 0);
    }
    close(client_fd);
}

int main() {
    Topic topic;
    ThreadPool pool(4);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(9001);

    bind(server_fd, (sockaddr*)&address, sizeof(address));
    listen(server_fd, 10);

    std::cout << "StreamForge broker listening on port 9001\n";

    while (true) {
        sockaddr_in client_address{};
        socklen_t client_len = sizeof(client_address);
        int client_fd = accept(server_fd, (sockaddr*)&client_address, &client_len);
        if (client_fd < 0) continue;

        pool.submit([client_fd, &topic]() {
            handleClient(client_fd, topic);
        });
    }

    close(server_fd);
    return 0;
}