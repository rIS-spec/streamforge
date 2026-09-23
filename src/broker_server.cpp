#include <iostream>
#include <string>
#include <vector>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include "message.h"
#include "topic.h"
#include "thread_pool.h"
#include "protocol.h"

void handleClient(int client_fd, Topic& topic) {
    std::vector<uint8_t> buffer;
    char temp[1024];

    while (true) {
        ssize_t bytes_read = recv(client_fd, temp, sizeof(temp), 0);
        if (bytes_read <= 0) break;

        buffer.insert(buffer.end(), temp, temp + bytes_read);

        while (true) {
            DecodedPacket decoded = decodePacket(buffer.data(), buffer.size());
            if (!decoded.valid) break;

            std::string response;
            if (decoded.payload.starts_with("PRODUCE ")) {
                Message m;
                m.payload = decoded.payload.substr(8);
                topic.publish(m);
                response = "OK";
            } else if (decoded.payload == "CONSUME") {
                Message out;
                if (topic.consume(out)) {
                    response = "MSG: " + out.payload;
                } else {
                    response = "EMPTY";
                }
            } else {
                response = "ERR unknown command";
            }

            auto reply = encodePacket(response);
            send(client_fd, reply.data(), reply.size(), 0);

            size_t consumed = 8 + decoded.payload.size();
            buffer.erase(buffer.begin(), buffer.begin() + consumed);
        }
    }
    close(client_fd);
}

int main() {
    Topic topic("streamforge_main.log");
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