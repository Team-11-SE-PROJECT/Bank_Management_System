#pragma once

#include <charconv>
#include <cstddef>
#include <string>
#include <system_error>
#include <vector>

namespace bms::infra::format {

inline constexpr std::size_t kAccountFields = 6;
inline constexpr std::size_t kCustomerFields = 4;
inline constexpr std::size_t kTransactionFields = 8;

inline std::vector<std::string> split(const std::string& line) {
    std::vector<std::string> fields;
    std::size_t start = 0;

    for (;;) {
        const std::size_t delim = line.find('|', start);
        if (delim == std::string::npos) {
            fields.push_back(line.substr(start));
            break;
        }
        fields.push_back(line.substr(start, delim - start));
        start = delim + 1;
    }

    return fields;
}

inline std::string join(const std::vector<std::string>& fields) {
    std::string line;

    for (std::size_t i = 0; i < fields.size(); ++i) {
        if (i) line += '|';
        line += fields[i];
    }

    return line;
}

inline bool parseNonNegative(
    const std::string& text, long long& out) {
    if (text.empty()) return false;

    for (char c : text) {
        if (c < '0' || c > '9') return false;
    }

    long long value = 0;
    const auto result = std::from_chars(
        text.data(), text.data() + text.size(), value);

    if (result.ec != std::errc{} ||
        result.ptr != text.data() + text.size() ||
        value < 0) {
        return false;
    }

    out = value;
    return true;
}

inline bool isSafeField(const std::string& text) {
    return text.find('|') == std::string::npos &&
           text.find('\n') == std::string::npos &&
           text.find('\r') == std::string::npos;
}

inline bool digits(
    const std::string& s, std::size_t begin, std::size_t end) {
    if (end > s.size() || begin > end) return false;

    for (std::size_t i = begin; i < end; ++i) {
        if (s[i] < '0' || s[i] > '9') return false;
    }

    return true;
}

inline bool isDate(const std::string& s) {
    if (s.size() != 10 || s[4] != '-' || s[7] != '-' ||
        !digits(s, 0, 4) || !digits(s, 5, 7) ||
        !digits(s, 8, 10)) {
        return false;
    }

    const int year = std::stoi(s.substr(0, 4));
    const int month = std::stoi(s.substr(5, 2));
    const int day = std::stoi(s.substr(8, 2));

    if (year < 1 || month < 1 || month > 12) return false;

    const bool leap = (year % 4 == 0 && year % 100 != 0)
                   || year % 400 == 0;

    const int days[] = {
        31, leap ? 29 : 28, 31, 30, 31, 30,
        31, 31, 30, 31, 30, 31
    };

    return day >= 1 && day <= days[month - 1];
}

inline bool isTimestamp(const std::string& s) {
    if (s.size() != 19 || s[10] != ' ' ||
        s[13] != ':' || s[16] != ':' ||
        !isDate(s.substr(0, 10)) ||
        !digits(s, 11, 13) ||
        !digits(s, 14, 16) ||
        !digits(s, 17, 19)) {
        return false;
    }

    const int hour = std::stoi(s.substr(11, 2));
    const int minute = std::stoi(s.substr(14, 2));
    const int second = std::stoi(s.substr(17, 2));

    return hour < 24 && minute < 60 && second < 60;
}

} // namespace bms::infra::format