#pragma once

#include <string>
#include <utility>
#include <vector>

#include "domain/JournalEntry.h"
#include "infra/AccountRepository.h"
#include "infra/AuditLogger.h"
#include "infra/LoadStatus.h"
#include "infra/TransactionRepository.h"

namespace bms::infra {

class Journal {
public:
    explicit Journal(std::string path)
        : path_(std::move(path)) {}

    LoadStatus load();
    bool append(const domain::JournalEntry& entry);
    bool clearAndReset();

    const std::vector<domain::JournalEntry>& entries() const {
        return entries_;
    }

    const std::string& path() const {
        return path_;
    }

    const std::string& lastError() const {
        return error_;
    }

    const LoadStatus& lastStatus() const {
        return status_;
    }

    std::string nextJournalId() const;

    // Startup crash recovery protocol:
    // Resolves all uncommitted journal entries (PENDING, APPLIED)
    // Ensures accounts and transactions are reconciled.
    // Idempotent: repeated calls do nothing if already recovered.
    static int recover(
        Journal& journal,
        AccountRepository& accounts,
        TransactionRepository& txns,
        const AuditLogger* audit = nullptr
    );

private:
    std::string path_;
    std::vector<domain::JournalEntry> entries_;
    std::string error_;
    LoadStatus status_;
};

} // namespace bms::infra
