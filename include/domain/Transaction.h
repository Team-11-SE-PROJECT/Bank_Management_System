#pragma once

#include <string>

namespace bms::domain {

enum class TxnType {
    DEPOSIT,
    WITHDRAWAL
};

enum class TxnState {
    PENDING,
    COMMITTED,
    ABORTED
};

struct Transaction {
    std::string txnId;
    TxnType type = TxnType::DEPOSIT;
    std::string accountNo;
    std::string counterparty = "-";
    long long amountPaise = 0;
    long long balanceAfterPaise = 0;
    std::string timestamp;
    TxnState state = TxnState::PENDING;
};

inline const char* toString(TxnType type) {
    switch (type) {
        case TxnType::DEPOSIT: return "DEPOSIT";
        case TxnType::WITHDRAWAL: return "WITHDRAWAL";
    }
    return "DEPOSIT";
}

inline const char* toString(TxnState state) {
    switch (state) {
        case TxnState::PENDING: return "PENDING";
        case TxnState::COMMITTED: return "COMMITTED";
        case TxnState::ABORTED: return "ABORTED";
    }
    return "ABORTED";
}

inline bool txnTypeFromString(
    const std::string& text,
    TxnType& out
) {
    if (text == "DEPOSIT") {
        out = TxnType::DEPOSIT;
        return true;
    }

    if (text == "WITHDRAWAL") {
        out = TxnType::WITHDRAWAL;
        return true;
    }

    return false;
}

inline bool txnStateFromString(
    const std::string& text,
    TxnState& out
) {
    if (text == "PENDING") {
        out = TxnState::PENDING;
        return true;
    }

    if (text == "COMMITTED") {
        out = TxnState::COMMITTED;
        return true;
    }

    if (text == "ABORTED") {
        out = TxnState::ABORTED;
        return true;
    }

    return false;
}

} // namespace bms::domain