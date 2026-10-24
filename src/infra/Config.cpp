#include "infra/Config.h"

#include <fstream>
#include <string>

namespace bms::infra {

LoadStatus Config::load(const std::string& path) {
    std::ifstream in(path);

    error_.clear();

    if (!in) {
        return LoadStatus::MISSING;
    }

    std::string line;
    int lineNo = 0;
    long long parsedLimit = dailyLimitPaise_;

    while (std::getline(in, line)) {
        ++lineNo;

        const auto first = line.find_first_not_of(" \t\r");
        if (first == std::string::npos || line[first] == '#') {
            continue;
        }

        const auto equal = line.find('=');
        if (equal == std::string::npos) {
            error_ = path + " line " + std::to_string(lineNo)
                     + ": expected key=value";
            return LoadStatus::CORRUPT;
        }

        const std::string key = line.substr(0, equal);
        const std::string value = line.substr(equal + 1);

        if (key.find("daily_withdrawal_limit_paise") !=
            std::string::npos) {
            try {
                std::size_t used = 0;
                const long long n = std::stoll(value, &used);

                if (used != value.size() || n < 0) {
                    throw std::invalid_argument("invalid limit");
                }

                parsedLimit = n;
            } catch (...) {
                error_ = path + " line " + std::to_string(lineNo)
                         + ": invalid withdrawal limit";
                return LoadStatus::CORRUPT;
            }
        }
    }

    dailyLimitPaise_ = parsedLimit;
    return LoadStatus::OK;
}

long long Config::dailyWithdrawalLimitPaise() const {
    return dailyLimitPaise_;
}

void Config::setDailyWithdrawalLimitPaise(long long value) {
    if (value >= 0) {
        dailyLimitPaise_ = value;
    }
}

const std::string& Config::lastError() const {
    return error_;
}

} // namespace bms::infra