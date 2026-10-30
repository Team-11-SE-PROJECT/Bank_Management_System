#pragma once

#include <string>

namespace bms::domain {

enum class JournalState {
    PENDING,
    APPLIED,
    COMMITTED,
    ABORTED
};

struct JournalEntry {
    std::string journalId;
    std::string transactionId;
    std::string type; // "DEPOSIT" or "WITHDRAWAL"
    std::string fromAccountId = "-";
    std::string toAccountId;
    long long amountPaise = 0;
    JournalState state = JournalState::PENDING;
    std::string timestamp;
};

inline const char* toString(JournalState state) {
    switch (state) {
        case JournalState::PENDING: return "PENDING";
        case JournalState::APPLIED: return "APPLIED";
        case JournalState::COMMITTED: return "COMMITTED";
        case JournalState::ABORTED: return "ABORTED";
    }
    return "ABORTED";
}

inline bool journalStateFromString(const std::string& text, JournalState& out) {
    if (text == "PENDING") {
        out = JournalState::PENDING;
        return true;
    }
    if (text == "APPLIED") {
        out = JournalState::APPLIED;
        return true;
    }
    if (text == "COMMITTED") {
        out = JournalState::COMMITTED;
        return true;
    }
    if (text == "ABORTED") {
        out = JournalState::ABORTED;
        return true;
    }
    return false;
}

} // namespace bms::domain
