#pragma once

#include <iostream>
#include <string>

namespace bms::test {

inline int& failures() { static int f = 0; return f; }

#define CHECK(cond) do {                                                 \
    if (!(cond)) {                                                       \
        std::cerr << "FAIL " << __FILE__ << ":" << __LINE__              \
                  << "  " #cond "\n";                                    \
        ++::bms::test::failures();                                       \
    }                                                                    \
} while (0)

#define CHECK_EQ(a, b) do {                                              \
    if (!((a) == (b))) {                                                 \
        std::cerr << "FAIL " << __FILE__ << ":" << __LINE__              \
                  << "  " #a " == " #b "\n";                             \
        ++::bms::test::failures();                                       \
    }                                                                    \
} while (0)

inline int summary(const char* name) {
    if (failures() == 0) {
        std::cout << name << ": all checks passed\n";
        return 0;
    }
    std::cerr << name << ": " << failures() << " failure(s)\n";
    return 1;
}

} // namespace bms::test