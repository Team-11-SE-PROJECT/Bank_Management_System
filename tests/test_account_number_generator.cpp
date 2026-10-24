#include <iostream>
#include <stdexcept>
#include <string>

#include "service/AccountNumberGenerator.h"

int main() {
    using bms::service::AccountNumberGenerator;

    if (AccountNumberGenerator::generate(1) != "BMS000001") {
        std::cerr << "Failed: first account number\n";
        return 1;
    }

    if (AccountNumberGenerator::generate(2) != "BMS000002") {
        std::cerr << "Failed: second account number\n";
        return 1;
    }

    if (AccountNumberGenerator::generate(999999) != "BMS999999") {
        std::cerr << "Failed: six-digit account number\n";
        return 1;
    }

    bool rejectedZero = false;

    try {
        AccountNumberGenerator::generate(0);
    } catch (const std::invalid_argument&) {
        rejectedZero = true;
    }

    if (!rejectedZero) {
        std::cerr << "Failed: zero sequence should be rejected\n";
        return 1;
    }

    std::cout << "test_account_number_generator: all checks passed\n";
    return 0;
}
