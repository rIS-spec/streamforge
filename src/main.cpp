#include <iostream>
#include <string>
#include "message.h"
#include "topic.h"

int main() {
    Topic topic("streamforge_main.log");
    std::string line;

    while (std::getline(std::cin, line)) {
        if (line == "exit")
            break;

        if (line.starts_with("produce ")) {
            Message m;
            m.payload = line.substr(8);
            topic.publish(m);
            std::cout << "Published: " << m.payload << "\n";
        } else if (line == "consume") {
            Message out;
            if (topic.consume(out)) {
                std::cout << "Consumed: " << out.payload << "\n";
            } else {
                std::cout << "(empty)\n";
            }
        } else {
            std::cout << "Unknown command\n";
        }
    }
    return 0;
}