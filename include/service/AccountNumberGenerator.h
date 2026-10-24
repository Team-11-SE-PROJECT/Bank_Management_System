
#pragma once

#include <string>

namespace bms::service {

class AccountNumberGenerator {
public:
    static std::string generate(unsigned long long sequence);
};

} // namespace bms::service
