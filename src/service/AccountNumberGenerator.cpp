
#include "service/AccountNumberGenerator.h"

#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace bms::service {

std::string AccountNumberGenerator::generate(
    unsigned long long sequence
) {
    if (sequence == 0 || sequence > 999999) {
        throw std::invalid_argument(
            "Sequence must be between 1 and 999999"
        );
    }

    std::ostringstream result;
    result << "BMS" << std::setw(6) << std::setfill('0') << sequence;

    return result.str();
}

} // namespace bms::service
