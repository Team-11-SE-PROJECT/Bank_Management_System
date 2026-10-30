#pragma once

#include <string>
#include <vector>

#include "domain/Account.h"
#include "domain/Customer.h"
#include "domain/JournalEntry.h"
#include "domain/Transaction.h"

namespace bms::infra {

struct IntegrityIssue {
    int lineNumber = 0;
    std::string field;
    std::string message;
};

struct ValidationResult {
    bool valid = true;
    std::vector<IntegrityIssue> errors;
    std::vector<IntegrityIssue> warnings;

    void addError(int line, std::string field, std::string msg) {
        valid = false;
        errors.push_back({line, std::move(field), std::move(msg)});
    }

    void addWarning(int line, std::string field, std::string msg) {
        warnings.push_back({line, std::move(field), std::move(msg)});
    }
};

class DataIntegrityValidator {
public:
    static ValidationResult validateCustomers(const std::string& contents);
    static ValidationResult validateAccounts(const std::string& contents);
    static ValidationResult validateTransactions(const std::string& contents);
    static ValidationResult validateJournal(const std::string& contents);

    static ValidationResult validateCrossReferences(
        const std::vector<domain::Account>& accounts,
        const std::vector<domain::Customer>& customers
    );

    static ValidationResult validateTransactionReferences(
        const std::vector<domain::Transaction>& txns,
        const std::vector<domain::Account>& accounts
    );
};

} // namespace bms::infra
