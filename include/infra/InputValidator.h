#pragma once

#include <string>

namespace bms::infra {

inline constexpr long long kMaxAmountPaise =
    99'999'999'999LL;

class InputValidator {
public:
    static bool parseAmountPaise(
        const std::string& text,
        long long& outPaise
    );

    static std::string formatPaise(long long paise);
};

} // namespace bms::infra
