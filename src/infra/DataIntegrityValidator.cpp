#include "infra/DataIntegrityValidator.h"

#include <sstream>
#include <unordered_set>

#include "infra/DataFormat.h"

namespace bms::infra {

namespace {

void stripTrailingCr(std::string& line) {
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
}

} // namespace

ValidationResult DataIntegrityValidator::validateCustomers(const std::string& contents) {
    ValidationResult res;
    std::istringstream in(contents);
    std::string line;
    int lineNo = 0;
    std::unordered_set<std::string> seenCustomerIds;
    bool checkedHeader = false;

    while (std::getline(in, line)) {
        ++lineNo;
        stripTrailingCr(line);
        if (line.empty()) continue;

        if (!checkedHeader && format::isVersionHeader(line)) {
            checkedHeader = true;
            int ver = 0;
            std::string err;
            if (!format::parseVersionHeader(line, ver, err)) {
                res.addError(lineNo, "version", "Malformed version header: " + line);
            } else if (ver != format::kCustomerVersion) {
                res.addError(lineNo, "version", "Unsupported customer file version: " + std::to_string(ver));
            }
            continue;
        }
        checkedHeader = true;

        domain::Customer c;
        auto status = format::deserializeCustomer(line, c, lineNo);
        if (!status.isOk()) {
            res.addError(lineNo, "record", status.errorMessage);
            continue;
        }

        if (c.customerId.size() > 32) {
            res.addError(lineNo, "customerId", "Customer ID too long (max 32 chars)");
        }

        if (!seenCustomerIds.insert(c.customerId).second) {
            res.addError(lineNo, "customerId", "Duplicate customer ID: " + c.customerId);
        }
    }

    return res;
}

ValidationResult DataIntegrityValidator::validateAccounts(const std::string& contents) {
    ValidationResult res;
    std::istringstream in(contents);
    std::string line;
    int lineNo = 0;
    std::unordered_set<std::string> seenAccountNos;
    bool checkedHeader = false;

    while (std::getline(in, line)) {
        ++lineNo;
        stripTrailingCr(line);
        if (line.empty()) continue;

        if (!checkedHeader && format::isVersionHeader(line)) {
            checkedHeader = true;
            int ver = 0;
            std::string err;
            if (!format::parseVersionHeader(line, ver, err)) {
                res.addError(lineNo, "version", "Malformed version header: " + line);
            } else if (ver != format::kAccountVersion) {
                res.addError(lineNo, "version", "Unsupported account file version: " + std::to_string(ver));
            }
            continue;
        }
        checkedHeader = true;

        domain::Account a;
        auto status = format::deserializeAccount(line, a, lineNo);
        if (!status.isOk()) {
            res.addError(lineNo, "record", status.errorMessage);
            continue;
        }

        if (a.accountNo.find('-') != std::string::npos) {
            res.addError(lineNo, "accountNo", "Account number cannot contain negative signs: " + a.accountNo);
        }

        if (a.accountNo.size() > 32) {
            res.addError(lineNo, "accountNo", "Account number too long (max 32 chars)");
        }

        if (!seenAccountNos.insert(a.accountNo).second) {
            res.addError(lineNo, "accountNo", "Duplicate account number: " + a.accountNo);
        }
    }

    return res;
}

ValidationResult DataIntegrityValidator::validateTransactions(const std::string& contents) {
    ValidationResult res;
    std::istringstream in(contents);
    std::string line;
    int lineNo = 0;
    bool checkedHeader = false;

    while (std::getline(in, line)) {
        ++lineNo;
        stripTrailingCr(line);
        if (line.empty()) continue;

        if (!checkedHeader && format::isVersionHeader(line)) {
            checkedHeader = true;
            int ver = 0;
            std::string err;
            if (!format::parseVersionHeader(line, ver, err)) {
                res.addError(lineNo, "version", "Malformed version header: " + line);
            } else if (ver != format::kTransactionVersion) {
                res.addError(lineNo, "version", "Unsupported transaction file version: " + std::to_string(ver));
            }
            continue;
        }
        checkedHeader = true;

        domain::Transaction t;
        auto status = format::deserializeTransaction(line, t, lineNo);
        if (!status.isOk()) {
            res.addError(lineNo, "record", status.errorMessage);
            continue;
        }

        if (t.amountPaise <= 0 || t.amountPaise > kMaxAmountPaise) {
            res.addError(lineNo, "amountPaise", "Transaction amount must be between 1 and kMaxAmountPaise");
        }
    }

    return res;
}

ValidationResult DataIntegrityValidator::validateJournal(const std::string& contents) {
    ValidationResult res;
    std::istringstream in(contents);
    std::string line;
    int lineNo = 0;
    bool checkedHeader = false;

    while (std::getline(in, line)) {
        ++lineNo;
        stripTrailingCr(line);
        if (line.empty()) continue;

        if (!checkedHeader && format::isVersionHeader(line)) {
            checkedHeader = true;
            int ver = 0;
            std::string err;
            if (!format::parseVersionHeader(line, ver, err)) {
                res.addError(lineNo, "version", "Malformed version header: " + line);
            } else if (ver != format::kJournalVersion) {
                res.addError(lineNo, "version", "Unsupported journal file version: " + std::to_string(ver));
            }
            continue;
        }
        checkedHeader = true;

        domain::JournalEntry j;
        auto status = format::deserializeJournalEntry(line, j, lineNo);
        if (!status.isOk()) {
            res.addError(lineNo, "record", status.errorMessage);
            continue;
        }

        if (j.amountPaise <= 0 || j.amountPaise > kMaxAmountPaise) {
            res.addError(lineNo, "amountPaise", "Journal amount must be between 1 and kMaxAmountPaise");
        }
    }

    return res;
}

ValidationResult DataIntegrityValidator::validateCrossReferences(
    const std::vector<domain::Account>& accounts,
    const std::vector<domain::Customer>& customers
) {
    ValidationResult res;
    std::unordered_set<std::string> validCustomerIds;
    for (const auto& c : customers) {
        validCustomerIds.insert(c.customerId);
    }

    for (const auto& a : accounts) {
        if (validCustomerIds.find(a.customerId) == validCustomerIds.end()) {
            res.addError(0, "customerId",
                "Account " + a.accountNo + " references non-existent customer: " + a.customerId);
        }
    }

    return res;
}

ValidationResult DataIntegrityValidator::validateTransactionReferences(
    const std::vector<domain::Transaction>& txns,
    const std::vector<domain::Account>& accounts
) {
    ValidationResult res;
    std::unordered_set<std::string> validAccountNos;
    for (const auto& a : accounts) {
        validAccountNos.insert(a.accountNo);
    }

    for (const auto& t : txns) {
        if (validAccountNos.find(t.accountNo) == validAccountNos.end()) {
            res.addError(0, "accountNo",
                "Transaction " + t.txnId + " references non-existent account: " + t.accountNo);
        }
    }

    return res;
}

} // namespace bms::infra
