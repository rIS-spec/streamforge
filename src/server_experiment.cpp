#include <iostream>
// POSIX headers
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>

int main() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "Failed to create socket\n";
        return 1;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(9001);

    if (bind(server_fd, (sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "Bind failed\n";
        close(server_fd);
        return 1;
    }

    std::cout << "Successfully bound to port 9001\n";

    if (listen(server_fd, 5) < 0) {
        std::cerr << "Listen failed\n";
        close(server_fd);
        return 1;
    }

    std::cout << "Listening on port 9001... (press Ctrl+C to stop)\n";
    std::cout << "Waiting for a connection...\n";

    sockaddr_in client_address{};
    socklen_t client_len = sizeof(client_address);
    int client_fd = accept(server_fd, (sockaddr*)&client_address, &client_len);

    if (client_fd < 0) {
        std::cerr << "Accept failed\n";
        close(server_fd);
        return 1;
    }

    std::cout << "A client connected!\n";

    char buffer[1024] = {};
    ssize_t bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);

    if (bytes_read > 0) {
        std::cout << "Received " << bytes_read << " bytes: " << buffer << "\n";

        std::string response = "ACK: got your message\n";
        send(client_fd, response.c_str(), response.size(), 0);
    } else {
        std::cout << "No data received (client disconnected immediately)\n";
    }

    close(client_fd);
    close(server_fd);
    return 0;
}