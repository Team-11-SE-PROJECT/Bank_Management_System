#include "infra/Journal.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <utility>

#include "infra/DataFormat.h"
#include "infra/FileUtil.h"

namespace fs = std::filesystem;

namespace bms::infra {

namespace {

bool parseJournalNumber(const std::string& id, long long& number) {
    if (id.size() < 4 || id.compare(0, 3, "JRN") != 0) {
        return false;
    }
    return format::parseNonNegative(id.substr(3), number);
}

} // namespace

LoadStatus Journal::load() {
    entries_.clear();
    error_.clear();
    status_ = LoadStatus::OK;

    std::error_code ec;
    if (!fs::exists(path_, ec)) {
        status_ = LoadStatus(LoadErrorCode::MISSING, "Journal file not found: " + path_, 0, path_);
        return status_;
    }

    std::string contents;
    if (!readAll(path_, contents)) {
        error_ = "Cannot read journal file: " + path_;
        status_ = LoadStatus(LoadErrorCode::IO_ERROR, error_, 0, path_);
        return status_;
    }

    if (contents.empty()) {
        status_ = LoadStatus::OK;
        return status_;
    }

    std::istringstream in(contents);
    std::string line;
    int lineNo = 0;
    std::vector<domain::JournalEntry> parsed;
    bool checkedHeader = false;

    while (std::getline(in, line)) {
        ++lineNo;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) continue;

        if (!checkedHeader && format::isVersionHeader(line)) {
            checkedHeader = true;
            int version = 0;
            std::string verErr;
            if (!format::parseVersionHeader(line, version, verErr)) {
                error_ = path_ + " line " + std::to_string(lineNo) + ": " + verErr;
                std::string quarantine;
                quarantineFile(path_, quarantine);
                status_ = LoadStatus(LoadErrorCode::MALFORMED, error_, lineNo, path_, quarantine);
                return status_;
            }
            if (version > format::kJournalVersion) {
                error_ = path_ + " line " + std::to_string(lineNo) + ": Unsupported journal version " + std::to_string(version);
                status_ = LoadStatus(LoadErrorCode::UNSUPPORTED_VERSION, error_, lineNo, path_);
                return status_;
            }
            continue;
        }
        checkedHeader = true;

        domain::JournalEntry entry;
        auto parseStatus = format::deserializeJournalEntry(line, entry, lineNo);
        if (!parseStatus.isOk()) {
            error_ = path_ + " line " + std::to_string(lineNo) + ": " + parseStatus.errorMessage;
            std::string quarantine;
            quarantineFile(path_, quarantine);
            status_ = LoadStatus(LoadErrorCode::MALFORMED, error_, lineNo, path_, quarantine);
            return status_;
        }

        parsed.push_back(entry);
    }

    entries_ = std::move(parsed);
    status_ = LoadStatus::OK;
    return status_;
}

bool Journal::append(const domain::JournalEntry& entry) {
    std::string line;
    std::string err;
    if (!format::serializeJournalEntry(entry, line, err)) {
        return false;
    }

    std::error_code ec;
    const bool isNewFile = !fs::exists(path_, ec);

    {
        std::ofstream out(path_, std::ios::app | std::ios::binary);
        if (!out) return false;

        if (isNewFile) {
            out << format::makeVersionHeader(format::kJournalVersion) << '\n';
        }

        out << line << '\n';
        out.flush();
        if (!out) return false;
    }

    entries_.push_back(entry);
    return true;
}

bool Journal::clearAndReset() {
    entries_.clear();
    const std::string header = format::makeVersionHeader(format::kJournalVersion) + "\n";
    return atomicWrite(path_, header);
}

std::string Journal::nextJournalId() const {
    long long maxNumber = 0;
    for (const auto& e : entries_) {
        long long num = 0;
        if (parseJournalNumber(e.journalId, num) && num > maxNumber) {
            maxNumber = num;
        }
    }

    char buf[32];
    std::snprintf(buf, sizeof(buf), "JRN%06lld", maxNumber + 1);
    return buf;
}

int Journal::recover(
    Journal& journal,
    AccountRepository& accounts,
    TransactionRepository& txns,
    const AuditLogger* audit
) {
    auto status = journal.load();
    if (status.errorCode == LoadErrorCode::CORRUPT ||
        status.errorCode == LoadErrorCode::MALFORMED) {
        if (audit) {
            audit->log("JOURNAL_CORRUPT", "-", "QUARANTINED", status.errorMessage);
        }
        journal.clearAndReset();
        return 0;
    }

    if (journal.entries().empty()) {
        return 0;
    }

    // Map each transactionId to its latest journal entry
    std::map<std::string, domain::JournalEntry> latestByTxn;
    for (const auto& entry : journal.entries()) {
        latestByTxn[entry.transactionId] = entry;
    }

    // Build map of committed transaction IDs already in txns repository
    std::map<std::string, const domain::Transaction*> committedTxns;
    for (const auto& t : txns.all()) {
        if (t.state == domain::TxnState::COMMITTED) {
            committedTxns[t.txnId] = &t;
        }
    }

    int recoveredCount = 0;

    for (const auto& [txnId, entry] : latestByTxn) {
        // If already committed or aborted, no action needed
        if (entry.state == domain::JournalState::COMMITTED ||
            entry.state == domain::JournalState::ABORTED) {
            continue;
        }

        if (entry.state == domain::JournalState::PENDING) {
            // Check if transaction was actually committed in transactions.dat
            if (committedTxns.find(txnId) != committedTxns.end()) {
                // Was already committed in txns, mark COMMITTED in journal
                domain::JournalEntry jComm = entry;
                jComm.journalId = journal.nextJournalId();
                jComm.state = domain::JournalState::COMMITTED;
                journal.append(jComm);
                ++recoveredCount;
            } else {
                // Incomplete PENDING transaction: abort it
                domain::JournalEntry jAbort = entry;
                jAbort.journalId = journal.nextJournalId();
                jAbort.state = domain::JournalState::ABORTED;
                journal.append(jAbort);
                ++recoveredCount;

                if (audit) {
                    audit->log("RECOVERY_ABORT", entry.toAccountId, "ABORTED",
                        "Transaction " + txnId + " aborted during recovery");
                }
            }
        } else if (entry.state == domain::JournalState::APPLIED) {
            // In APPLIED state, the balance change was prepared/applied
            auto* acc = accounts.find(entry.toAccountId);
            if (!acc) {
                // Cannot find account, abort
                domain::JournalEntry jAbort = entry;
                jAbort.journalId = journal.nextJournalId();
                jAbort.state = domain::JournalState::ABORTED;
                journal.append(jAbort);
                ++recoveredCount;
                continue;
            }

            // Check if COMMITTED record exists in txns
            if (committedTxns.find(txnId) == committedTxns.end()) {
                // If accounts was not saved with the new balance, reconcile it
                bool balanceNeedsAdjustment = true;
                for (const auto& t : txns.all()) {
                    if (t.txnId == txnId && t.state == domain::TxnState::PENDING) {
                        acc->balancePaise = t.balanceAfterPaise;
                        balanceNeedsAdjustment = false;
                        break;
                    }
                }
                if (balanceNeedsAdjustment) {
                    if (entry.type == "DEPOSIT") {
                        acc->balancePaise += entry.amountPaise;
                    } else if (entry.type == "WITHDRAWAL") {
                        acc->balancePaise -= entry.amountPaise;
                    }
                }

                // Ensure account save is complete
                accounts.upsert(*acc);
                accounts.save();

                // Create COMMITTED transaction record in txns
                domain::Transaction t;
                t.txnId = txnId;
                t.accountNo = entry.toAccountId;
                t.counterparty = entry.fromAccountId;
                t.amountPaise = entry.amountPaise;
                t.balanceAfterPaise = acc->balancePaise;
                t.timestamp = entry.timestamp;
                t.state = domain::TxnState::COMMITTED;

                if (entry.type == "DEPOSIT") {
                    t.type = domain::TxnType::DEPOSIT;
                } else {
                    t.type = domain::TxnType::WITHDRAWAL;
                }

                txns.append(t);
                committedTxns[txnId] = &txns.all().back();

                // Mark COMMITTED in journal
                domain::JournalEntry jComm = entry;
                jComm.journalId = journal.nextJournalId();
                jComm.state = domain::JournalState::COMMITTED;
                journal.append(jComm);

                ++recoveredCount;

                if (audit) {
                    audit->log("RECOVERY_COMMIT", entry.toAccountId, "SUCCESS",
                        "Transaction " + txnId + " recovered and committed");
                }
            } else {
                // Record was already in txns, just mark COMMITTED in journal
                domain::JournalEntry jComm = entry;
                jComm.journalId = journal.nextJournalId();
                jComm.state = domain::JournalState::COMMITTED;
                journal.append(jComm);
                ++recoveredCount;
            }
        }
    }

    return recoveredCount;
}

} // namespace bms::infra
