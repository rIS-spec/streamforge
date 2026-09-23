#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "protocol.h"

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <command>\n";
        return 1;
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(9001);
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);

    connect(sock, (sockaddr*)&server_addr, sizeof(server_addr));

    std::string command = argv[1];
    auto packet = encodePacket(command);
    send(sock, packet.data(), packet.size(), 0);

    uint8_t buffer[1024];
    ssize_t bytes_read = recv(sock, buffer, sizeof(buffer), 0);

    if (bytes_read > 0) {
        DecodedPacket decoded = decodePacket(buffer, bytes_read);
        if (decoded.valid) {
            std::cout << "Response: " << decoded.payload << "\n";
        } else {
            std::cout << "Received invalid/incomplete response\n";
        }
    }

    close(sock);
    return 0;
}