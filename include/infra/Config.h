#pragma once

#include <string>
#include "infra/LoadStatus.h"

namespace bms::infra {

class Config {
public:
    LoadStatus load(const std::string& path);
    long long dailyWithdrawalLimitPaise() const;
    void setDailyWithdrawalLimitPaise(long long value);
    const std::string& lastError() const;

private:
    long long dailyLimitPaise_ = 2500000;
    std::string error_;
};

} // namespace bms::infra