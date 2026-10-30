#pragma once

#include <charconv>
#include <cstddef>
#include <string>
#include <system_error>
#include <vector>

#include "domain/Account.h"
#include "domain/Customer.h"
#include "domain/JournalEntry.h"
#include "domain/Transaction.h"
#include "infra/InputValidator.h"
#include "infra/LoadStatus.h"

namespace bms::infra::format {

// ============================================================================
// FIELD SCHEMAS (Pipe-delimited, '|'):
//
// 1. Customer:
//    customerId | name | phone | address
//    - customerId : string, non-empty, no pipes/newlines (e.g., "CUST0001")
//    - name       : string, non-empty, no pipes/newlines
//    - phone      : string, 10 digits, no pipes/newlines
//    - address    : string, optional/freeform, no pipes/newlines
//
// 2. Account:
//    accountNo | customerId | balancePaise | status | withdrawnTodayPaise | lastWithdrawDate
//    - accountNo           : string, non-empty, no pipes/newlines (e.g., "BMS000001")
//    - customerId          : string, non-empty, foreign key to Customer
//    - balancePaise        : int64_t, non-negative integer paise [0, kMaxAmountPaise]
//    - status              : string, "ACTIVE" or "CLOSED"
//    - withdrawnTodayPaise : int64_t, non-negative integer paise [0, kMaxAmountPaise]
//    - lastWithdrawDate    : string, "YYYY-MM-DD" or "-"
//
// 3. Transaction:
//    txnId | type | accountNo | counterparty | amountPaise | balanceAfterPaise | timestamp | state
//    - txnId             : string, non-empty (e.g., "TXN000001")
//    - type              : string, "DEPOSIT" or "WITHDRAWAL"
//    - accountNo         : string, non-empty
//    - counterparty      : string, "CASH", "CHEQUE", account number, or "-"
//    - amountPaise       : int64_t, positive integer paise (> 0)
//    - balanceAfterPaise : int64_t, non-negative integer paise
//    - timestamp         : string, "YYYY-MM-DD HH:MM:SS"
//    - state             : string, "PENDING", "COMMITTED", or "ABORTED"
//
// 4. Journal:
//    journalId | transactionId | type | fromAccountId | toAccountId | amountPaise | state | timestamp
//    - journalId     : string, non-empty (e.g., "JRN000001")
//    - transactionId : string, non-empty (e.g., "TXN000001")
//    - type          : string, "DEPOSIT" or "WITHDRAWAL"
//    - fromAccountId : string, account number or "-"
//    - toAccountId   : string, account number
//    - amountPaise   : int64_t, positive integer paise (> 0)
//    - state         : string, "PENDING", "APPLIED", "COMMITTED", or "ABORTED"
//    - timestamp     : string, "YYYY-MM-DD HH:MM:SS"
// ============================================================================

// Field count constants
inline constexpr std::size_t kCustomerFields = 4;
inline constexpr std::size_t kAccountFields = 6;
inline constexpr std::size_t kTransactionFields = 8;
inline constexpr std::size_t kJournalFields = 8;

// Version constants per entity type
inline constexpr int kCustomerVersion = 1;
inline constexpr int kAccountVersion = 1;
inline constexpr int kTransactionVersion = 1;
inline constexpr int kJournalVersion = 1;

// Format version header prefix
inline constexpr const char* kVersionPrefix = "#VERSION ";

// ----------------------------------------------------------------------------
// Basic string utilities
// ----------------------------------------------------------------------------

inline std::vector<std::string> split(const std::string& line) {
    std::vector<std::string> fields;
    std::size_t start = 0;

    for (;;) {
        const std::size_t delim = line.find('|', start);
        if (delim == std::string::npos) {
            fields.push_back(line.substr(start));
            break;
        }
        fields.push_back(line.substr(start, delim - start));
        start = delim + 1;
    }

    return fields;
}

inline std::string join(const std::vector<std::string>& fields) {
    std::string line;

    for (std::size_t i = 0; i < fields.size(); ++i) {
        if (i) line += '|';
        line += fields[i];
    }

    return line;
}

inline bool isSafeField(const std::string& text) {
    return text.find('|') == std::string::npos &&
           text.find('\n') == std::string::npos &&
           text.find('\r') == std::string::npos;
}

inline bool digits(const std::string& s, std::size_t begin, std::size_t end) {
    if (end > s.size() || begin > end) return false;

    for (std::size_t i = begin; i < end; ++i) {
        if (s[i] < '0' || s[i] > '9') return false;
    }

    return true;
}

inline bool isAllDigits(const std::string& s) {
    if (s.empty()) return false;
    return digits(s, 0, s.size());
}

inline bool parseNonNegative(const std::string& text, long long& out) {
    if (text.empty()) return false;

    for (char c : text) {
        if (c < '0' || c > '9') return false;
    }

    long long value = 0;
    const auto result = std::from_chars(
        text.data(), text.data() + text.size(), value);

    if (result.ec != std::errc{} ||
        result.ptr != text.data() + text.size() ||
        value < 0) {
        return false;
    }

    out = value;
    return true;
}

inline bool isDate(const std::string& s) {
    if (s.size() != 10 || s[4] != '-' || s[7] != '-' ||
        !digits(s, 0, 4) || !digits(s, 5, 7) ||
        !digits(s, 8, 10)) {
        return false;
    }

    const int year = std::stoi(s.substr(0, 4));
    const int month = std::stoi(s.substr(5, 2));
    const int day = std::stoi(s.substr(8, 2));

    if (year < 1 || month < 1 || month > 12) return false;

    const bool leap = (year % 4 == 0 && year % 100 != 0)
                   || year % 400 == 0;

    const int days[] = {
        31, leap ? 29 : 28, 31, 30, 31, 30,
        31, 31, 30, 31, 30, 31
    };

    return day >= 1 && day <= days[month - 1];
}

inline bool isTimestamp(const std::string& s) {
    if (s.size() != 19 || s[10] != ' ' ||
        s[13] != ':' || s[16] != ':' ||
        !isDate(s.substr(0, 10)) ||
        !digits(s, 11, 13) ||
        !digits(s, 14, 16) ||
        !digits(s, 17, 19)) {
        return false;
    }

    const int hour = std::stoi(s.substr(11, 2));
    const int minute = std::stoi(s.substr(14, 2));
    const int second = std::stoi(s.substr(17, 2));

    return hour < 24 && minute < 60 && second < 60;
}

// ----------------------------------------------------------------------------
// Version Header Helpers
// ----------------------------------------------------------------------------

inline bool isVersionHeader(const std::string& line) {
    std::string_view prefix = kVersionPrefix;
    return line.size() >= prefix.size() &&
           line.compare(0, prefix.size(), prefix) == 0;
}

inline bool parseVersionHeader(const std::string& line, int& versionOut, std::string& error) {
    std::string_view prefix = kVersionPrefix;
    if (!isVersionHeader(line)) {
        error = "Not a version header line";
        return false;
    }
    const std::string verStr = line.substr(prefix.size());
    if (verStr.empty() || !isAllDigits(verStr)) {
        error = "Invalid version header format: " + line;
        return false;
    }
    try {
        versionOut = std::stoi(verStr);
        if (versionOut <= 0) {
            error = "Version number must be positive: " + verStr;
            return false;
        }
        return true;
    } catch (...) {
        error = "Failed to parse version number: " + verStr;
        return false;
    }
}

inline std::string makeVersionHeader(int version) {
    return std::string(kVersionPrefix) + std::to_string(version);
}

// ----------------------------------------------------------------------------
// Serialization Helpers
// ----------------------------------------------------------------------------

inline bool serializeCustomer(const domain::Customer& c, std::string& out, std::string& error) {
    if (c.customerId.empty() || !isSafeField(c.customerId)) {
        error = "Invalid customerId";
        return false;
    }
    if (c.name.empty() || !isSafeField(c.name)) {
        error = "Invalid customer name";
        return false;
    }
    if (c.phone.empty() || !isSafeField(c.phone) || c.phone.size() != 10 || !isAllDigits(c.phone)) {
        error = "Invalid customer phone (must be 10 digits)";
        return false;
    }
    if (!isSafeField(c.address)) {
        error = "Invalid customer address (contains delimiter or newline)";
        return false;
    }

    out = join({c.customerId, c.name, c.phone, c.address});
    return true;
}

inline bool serializeAccount(const domain::Account& a, std::string& out, std::string& error) {
    if (a.accountNo.empty() || !isSafeField(a.accountNo)) {
        error = "Invalid accountNo";
        return false;
    }
    if (a.customerId.empty() || !isSafeField(a.customerId)) {
        error = "Invalid customerId";
        return false;
    }
    if (a.balancePaise < 0) {
        error = "Balance cannot be negative";
        return false;
    }
    if (a.status != "ACTIVE" && a.status != "CLOSED") {
        error = "Invalid account status (must be ACTIVE or CLOSED)";
        return false;
    }
    if (a.withdrawnTodayPaise < 0 || a.withdrawnTodayPaise > kMaxAmountPaise) {
        error = "withdrawnTodayPaise out of range [0, kMaxAmountPaise]";
        return false;
    }
    const std::string dateStr = a.lastWithdrawDate.empty() ? "-" : a.lastWithdrawDate;
    if (dateStr != "-" && !isDate(dateStr)) {
        error = "lastWithdrawDate must be YYYY-MM-DD or -";
        return false;
    }

    out = join({
        a.accountNo,
        a.customerId,
        std::to_string(a.balancePaise),
        a.status,
        std::to_string(a.withdrawnTodayPaise),
        dateStr
    });
    return true;
}

inline bool serializeTransaction(const domain::Transaction& t, std::string& out, std::string& error) {
    if (t.txnId.empty() || !isSafeField(t.txnId)) {
        error = "Invalid txnId";
        return false;
    }
    if (t.accountNo.empty() || !isSafeField(t.accountNo)) {
        error = "Invalid accountNo";
        return false;
    }
    if (!isSafeField(t.counterparty)) {
        error = "Invalid counterparty";
        return false;
    }
    if (t.amountPaise <= 0 || t.amountPaise > kMaxAmountPaise) {
        error = "amountPaise must be positive and <= kMaxAmountPaise";
        return false;
    }
    if (t.balanceAfterPaise < 0) {
        error = "balanceAfterPaise must be non-negative";
        return false;
    }
    if (!isTimestamp(t.timestamp)) {
        error = "Invalid timestamp format";
        return false;
    }

    out = join({
        t.txnId,
        domain::toString(t.type),
        t.accountNo,
        t.counterparty.empty() ? "-" : t.counterparty,
        std::to_string(t.amountPaise),
        std::to_string(t.balanceAfterPaise),
        t.timestamp,
        domain::toString(t.state)
    });
    return true;
}

inline bool serializeJournalEntry(const domain::JournalEntry& j, std::string& out, std::string& error) {
    if (j.journalId.empty() || !isSafeField(j.journalId)) {
        error = "Invalid journalId";
        return false;
    }
    if (j.transactionId.empty() || !isSafeField(j.transactionId)) {
        error = "Invalid transactionId";
        return false;
    }
    if (j.type != "DEPOSIT" && j.type != "WITHDRAWAL") {
        error = "Invalid type (must be DEPOSIT or WITHDRAWAL)";
        return false;
    }
    if (!isSafeField(j.fromAccountId)) {
        error = "Invalid fromAccountId";
        return false;
    }
    if (j.toAccountId.empty() || !isSafeField(j.toAccountId)) {
        error = "Invalid toAccountId";
        return false;
    }
    if (j.amountPaise <= 0 || j.amountPaise > kMaxAmountPaise) {
        error = "amountPaise must be positive and <= kMaxAmountPaise";
        return false;
    }
    if (!isTimestamp(j.timestamp)) {
        error = "Invalid timestamp";
        return false;
    }

    out = join({
        j.journalId,
        j.transactionId,
        j.type,
        j.fromAccountId.empty() ? "-" : j.fromAccountId,
        j.toAccountId,
        std::to_string(j.amountPaise),
        domain::toString(j.state),
        j.timestamp
    });
    return true;
}

// ----------------------------------------------------------------------------
// Deserialization Helpers (returning LoadStatus)
// ----------------------------------------------------------------------------

inline LoadStatus deserializeCustomer(const std::string& line, domain::Customer& out, int lineNo = 0) {
    const auto f = split(line);
    if (f.size() != kCustomerFields) {
        return LoadStatus(LoadErrorCode::MALFORMED,
            "Expected 4 customer fields but found " + std::to_string(f.size()), lineNo);
    }
    if (f[0].empty() || !isSafeField(f[0])) {
        return LoadStatus(LoadErrorCode::MALFORMED, "Customer ID must not be empty", lineNo);
    }
    if (f[1].empty() || !isSafeField(f[1])) {
        return LoadStatus(LoadErrorCode::MALFORMED, "Customer name must not be empty", lineNo);
    }
    if (f[2].empty() || !isSafeField(f[2]) || f[2].size() != 10 || !isAllDigits(f[2])) {
        return LoadStatus(LoadErrorCode::MALFORMED, "Customer phone must be 10 digits", lineNo);
    }
    if (!isSafeField(f[3])) {
        return LoadStatus(LoadErrorCode::MALFORMED, "Customer address contains invalid characters", lineNo);
    }

    out.customerId = f[0];
    out.name = f[1];
    out.phone = f[2];
    out.address = f[3];
    return LoadStatus::OK;
}

inline LoadStatus deserializeAccount(const std::string& line, domain::Account& out, int lineNo = 0) {
    const auto f = split(line);
    if (f.size() != kAccountFields) {
        return LoadStatus(LoadErrorCode::MALFORMED,
            "Expected 6 account fields but found " + std::to_string(f.size()), lineNo);
    }
    if (f[0].empty() || !isSafeField(f[0])) {
        return LoadStatus(LoadErrorCode::MALFORMED, "Account number must not be empty", lineNo);
    }
    if (f[1].empty() || !isSafeField(f[1])) {
        return LoadStatus(LoadErrorCode::MALFORMED, "Customer ID must not be empty", lineNo);
    }
    long long balance = 0;
    if (!parseNonNegative(f[2], balance)) {
        return LoadStatus(LoadErrorCode::MALFORMED, "Balance must be a non-negative whole number of paise", lineNo);
    }
    if (f[3] != "ACTIVE" && f[3] != "CLOSED") {
        return LoadStatus(LoadErrorCode::MALFORMED, "Account status must be ACTIVE or CLOSED", lineNo);
    }
    long long withdrawn = 0;
    if (!parseNonNegative(f[4], withdrawn) || withdrawn > kMaxAmountPaise) {
        return LoadStatus(LoadErrorCode::MALFORMED, "Withdrawn today must be a non-negative whole number of paise within limit", lineNo);
    }
    if (f[5] != "-" && !isDate(f[5])) {
        return LoadStatus(LoadErrorCode::MALFORMED, "lastWithdrawDate must be YYYY-MM-DD or -", lineNo);
    }

    out.accountNo = f[0];
    out.customerId = f[1];
    out.balancePaise = balance;
    out.status = f[3];
    out.withdrawnTodayPaise = withdrawn;
    out.lastWithdrawDate = (f[5] == "-") ? "" : f[5];
    return LoadStatus::OK;
}

inline LoadStatus deserializeTransaction(const std::string& line, domain::Transaction& out, int lineNo = 0) {
    const auto f = split(line);
    if (f.size() != kTransactionFields) {
        return LoadStatus(LoadErrorCode::MALFORMED,
            "Expected 8 transaction fields but found " + std::to_string(f.size()), lineNo);
    }
    if (f[0].empty() || !isSafeField(f[0])) {
        return LoadStatus(LoadErrorCode::MALFORMED, "Transaction ID must not be empty", lineNo);
    }
    domain::TxnType type;
    if (!domain::txnTypeFromString(f[1], type)) {
        return LoadStatus(LoadErrorCode::MALFORMED, "Invalid transaction type: " + f[1], lineNo);
    }
    if (f[2].empty() || !isSafeField(f[2])) {
        return LoadStatus(LoadErrorCode::MALFORMED, "Account number must not be empty", lineNo);
    }
    if (!isSafeField(f[3])) {
        return LoadStatus(LoadErrorCode::MALFORMED, "Invalid counterparty", lineNo);
    }
    long long amount = 0;
    if (!parseNonNegative(f[4], amount) || amount <= 0 || amount > kMaxAmountPaise) {
        return LoadStatus(LoadErrorCode::MALFORMED, "Amount must be a positive integer in paise", lineNo);
    }
    long long balanceAfter = 0;
    if (!parseNonNegative(f[5], balanceAfter)) {
        return LoadStatus(LoadErrorCode::MALFORMED, "balanceAfter must be non-negative integer in paise", lineNo);
    }
    if (!isTimestamp(f[6])) {
        return LoadStatus(LoadErrorCode::MALFORMED, "Invalid timestamp format: " + f[6], lineNo);
    }
    domain::TxnState state;
    if (!domain::txnStateFromString(f[7], state)) {
        return LoadStatus(LoadErrorCode::MALFORMED, "Invalid transaction state: " + f[7], lineNo);
    }

    out.txnId = f[0];
    out.type = type;
    out.accountNo = f[2];
    out.counterparty = f[3];
    out.amountPaise = amount;
    out.balanceAfterPaise = balanceAfter;
    out.timestamp = f[6];
    out.state = state;
    return LoadStatus::OK;
}

inline LoadStatus deserializeJournalEntry(const std::string& line, domain::JournalEntry& out, int lineNo = 0) {
    const auto f = split(line);
    if (f.size() != kJournalFields) {
        return LoadStatus(LoadErrorCode::MALFORMED,
            "Expected 8 journal fields but found " + std::to_string(f.size()), lineNo);
    }
    if (f[0].empty() || !isSafeField(f[0])) {
        return LoadStatus(LoadErrorCode::MALFORMED, "Journal ID must not be empty", lineNo);
    }
    if (f[1].empty() || !isSafeField(f[1])) {
        return LoadStatus(LoadErrorCode::MALFORMED, "Transaction ID must not be empty", lineNo);
    }
    if (f[2] != "DEPOSIT" && f[2] != "WITHDRAWAL") {
        return LoadStatus(LoadErrorCode::MALFORMED, "Invalid journal type: " + f[2], lineNo);
    }
    if (!isSafeField(f[3])) {
        return LoadStatus(LoadErrorCode::MALFORMED, "Invalid fromAccountId", lineNo);
    }
    if (f[4].empty() || !isSafeField(f[4])) {
        return LoadStatus(LoadErrorCode::MALFORMED, "toAccountId must not be empty", lineNo);
    }
    long long amount = 0;
    if (!parseNonNegative(f[5], amount) || amount <= 0 || amount > kMaxAmountPaise) {
        return LoadStatus(LoadErrorCode::MALFORMED, "Amount must be a positive integer in paise", lineNo);
    }
    domain::JournalState state;
    if (!domain::journalStateFromString(f[6], state)) {
        return LoadStatus(LoadErrorCode::MALFORMED, "Invalid journal state: " + f[6], lineNo);
    }
    if (!isTimestamp(f[7])) {
        return LoadStatus(LoadErrorCode::MALFORMED, "Invalid timestamp format: " + f[7], lineNo);
    }

    out.journalId = f[0];
    out.transactionId = f[1];
    out.type = f[2];
    out.fromAccountId = f[3];
    out.toAccountId = f[4];
    out.amountPaise = amount;
    out.state = state;
    out.timestamp = f[7];
    return LoadStatus::OK;
}

} // namespace bms::infra::format
