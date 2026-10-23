#include "infra/InputValidator.h"

namespace bms::infra {

namespace {

bool allDigits(const std::string& text) {
    if (text.empty()) {
        return false;
    }

    for (char ch : text) {
        if (ch < '0' || ch > '9') {
            return false;
        }
    }

    return true;
}

} // namespace

bool InputValidator::parseAmountPaise(
    const std::string& text,
    long long& outPaise
) {
    if (text.empty()) {
        return false;
    }

    const std::size_t dot = text.find('.');

    // Reject multiple decimal points.
    if (dot != std::string::npos &&
        text.find('.', dot + 1) != std::string::npos) {
        return false;
    }

    const bool hasDecimal = dot != std::string::npos;

    const std::string whole =
        hasDecimal ? text.substr(0, dot) : text;

    const std::string fraction =
        hasDecimal ? text.substr(dot + 1) : "";

    if (!allDigits(whole)) {
        return false;
    }

    if (hasDecimal &&
        (fraction.empty() ||
         fraction.size() > 2 ||
         !allDigits(fraction))) {
        return false;
    }

    // Parse rupees safely without overflowing.
    const long long maxRupees = kMaxAmountPaise / 100;
    long long rupees = 0;

    for (char ch : whole) {
        const int digit = ch - '0';

        if (rupees > (maxRupees - digit) / 10) {
            return false;
        }

        rupees = rupees * 10 + digit;
    }

    // Convert decimal digits to paise.
    long long fractionalPaise = 0;

    if (fraction.size() == 1) {
        fractionalPaise = (fraction[0] - '0') * 10;
    } else if (fraction.size() == 2) {
        fractionalPaise =
            (fraction[0] - '0') * 10 +
            (fraction[1] - '0');
    }

    const long long parsedPaise =
        rupees * 100 + fractionalPaise;

    if (parsedPaise <= 0 ||
        parsedPaise > kMaxAmountPaise) {
        return false;
    }

    // Change the output only after successful validation.
    outPaise = parsedPaise;
    return true;
}

std::string InputValidator::formatPaise(long long paise) {
    const bool negative = paise < 0;

    // Also handles the most-negative long long safely.
    const unsigned long long magnitude =
        negative
            ? 0ULL - static_cast<unsigned long long>(paise)
            : static_cast<unsigned long long>(paise);

    const unsigned long long rupees = magnitude / 100;
    const unsigned long long fraction = magnitude % 100;

    std::string result = negative ? "-" : "";
    result += std::to_string(rupees);
    result += '.';

    if (fraction < 10) {
        result += '0';
    }

    result += std::to_string(fraction);
    return result;
}

} // namespace bms::infra