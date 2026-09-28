#pragma once
#include <iostream>
#include <string>
#include <chrono>
#include <sstream>
#include <initializer_list>
#include <utility>

enum class LogLevel { DEBUG, INFO, WARN, ERROR };

inline const char* levelToString(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO";
        case LogLevel::WARN:  return "WARN";
        case LogLevel::ERROR: return "ERROR";
    }
    return "UNKNOWN";
}

// Minimal escaper for our own JSON-lines output. Intentionally simple:
// call sites in this project only pass internally-controlled strings
// (config values, fixed event names, strerror() output) — not raw,
// untrusted user/network input — so full RFC 8259 escaping isn't needed.
inline std::string escapeJson(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out;
}

// One JSON object per line: {"ts":<ms>,"level":"INFO","event":"...","k":"v",...}
inline void logEvent(LogLevel level, const std::string& event,
                      std::initializer_list<std::pair<std::string, std::string>> fields = {}) {
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    std::ostringstream out;
    out << "{\"ts\":" << now_ms
        << ",\"level\":\"" << levelToString(level) << "\""
        << ",\"event\":\"" << escapeJson(event) << "\"";
    for (const auto& [k, v] : fields) {
        out << ",\"" << escapeJson(k) << "\":\"" << escapeJson(v) << "\"";
    }
    out << "}";

    std::cout << out.str() << std::endl;
}