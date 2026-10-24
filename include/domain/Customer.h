
#pragma once

#include <string>

namespace bms::domain {

struct Customer {
    std::string customerId;
    std::string name;
    std::string phone;
    std::string address;
};

} // namespace bms::domain
