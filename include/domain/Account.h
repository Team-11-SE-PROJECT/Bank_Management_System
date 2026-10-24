
#pragma once

#include <string>

namespace bms::domain {

struct Account {
    std::string accountNo;
    std::string customerId;

    long long balancePaise = 0;

    std::string status = "ACTIVE";

    long long withdrawnTodayPaise = 0;
    std::string lastWithdrawDate;
};

} // namespace bms::domain
