#include <iostream>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "protocol.h"

// Sends one framed request and blocks for one framed response.
// Returns false if the connection dropped or the response was malformed.
bool sendCommand(int sock, const std::string& command, std::string& out_response) {
    auto packet = encodePacket(command);

    size_t sent = 0;
    while (sent < packet.size()) {
        ssize_t n = send(sock, packet.data() + sent, packet.size() - sent, 0);
        if (n <= 0)
            return false;
        sent += static_cast<size_t>(n);
    }

    // Accumulate until we have a full frame — a single recv() is not
    // guaranteed to return the whole response in one call.
    std::vector<uint8_t> buffer;
    uint8_t chunk[4096];
    while (true) {
        DecodedPacket decoded = decodePacket(buffer.data(), buffer.size());
        if (decoded.valid) {
            out_response = decoded.payload;
            return true;
        }
        ssize_t n = recv(sock, chunk, sizeof(chunk), 0);
        if (n <= 0)
            return false;  // connection closed or error mid-response
        buffer.insert(buffer.end(), chunk, chunk + n);
    }
}

int main(int argc, char* argv[]) {
    std::string host = "127.0.0.1";
    int port = 9001;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--host" && i + 1 < argc)
            host = argv[++i];
        else if (arg == "--port" && i + 1 < argc)
            port = std::stoi(argv[++i]);
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        std::cerr << "sf-client: failed to create socket\n";
        return 1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0) {
        std::cerr << "sf-client: invalid host address " << host << "\n";
        return 1;
    }

    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "sf-client: connect() failed — is the broker running on " << host << ":"
                  << port << "?\n";
        return 1;
    }

    std::cout << "sf-client connected to " << host << ":" << port << "\n";

    // Check for an unsolicited message from the server (e.g. a rejection)
    // before sending anything ourselves — reading first avoids racing our
    // own send() against a server that already closed the connection.
    timeval tv{0, 200000};  // 200ms
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    uint8_t peek_buf[512];
    ssize_t peek_n = recv(sock, peek_buf, sizeof(peek_buf), MSG_PEEK);
    if (peek_n > 0) {
        DecodedPacket decoded = decodePacket(peek_buf, static_cast<size_t>(peek_n));
        if (decoded.valid) {
            std::cout << decoded.payload << "\n";
            close(sock);
            return 1;
        }
    }

    tv = {0, 0};  // back to blocking for the normal REPL
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    std::cout << "Commands: PRODUCE <text> | CONSUME <partition> | HEALTH | METRICS | QUIT\n\n";

    std::string line;
    while (true) {
        std::cout << "sf> ";
        if (!std::getline(std::cin, line))
            break;  // EOF (Ctrl+D)

        if (line.empty())
            continue;
        if (line == "QUIT" || line == "quit" || line == "exit")
            break;

        std::string response;
        if (!sendCommand(sock, line, response)) {
            std::cerr << "sf-client: connection lost or malformed response — exiting\n";
            break;
        }
        std::cout << response << "\n";
    }

    close(sock);
    std::cout << "sf-client disconnected.\n";
    return 0;
}