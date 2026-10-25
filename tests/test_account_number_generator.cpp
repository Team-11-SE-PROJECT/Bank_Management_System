#include <iostream>
#include <stdexcept>
#include <string>

#include "service/AccountNumberGenerator.h"

int main() {
    using bms::service::AccountNumberGenerator;

    // Test 1: First account number
    if (AccountNumberGenerator::generate(1) != "BMS000001") {
        std::cerr << "Failed: first account number\n";
        return 1;
    }

    // Test 2: Second account number
    if (AccountNumberGenerator::generate(2) != "BMS000002") {
        std::cerr << "Failed: second account number\n";
        return 1;
    }

    // Test 3: Maximum valid six-digit sequence
    if (AccountNumberGenerator::generate(999999) != "BMS999999") {
        std::cerr << "Failed: maximum account number\n";
        return 1;
    }

    // Test 4: Zero sequence must be rejected
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

    // Test 5: Sequence above the six-digit limit must be rejected
    bool rejectedTooLarge = false;

    try {
        AccountNumberGenerator::generate(1000000);
    } catch (const std::invalid_argument&) {
        rejectedTooLarge = true;
    }

    if (!rejectedTooLarge) {
        std::cerr << "Failed: sequence above 999999 should be rejected\n";
        return 1;
    }

    std::cout << "test_account_number_generator: all checks passed\n";
    return 0;
}
