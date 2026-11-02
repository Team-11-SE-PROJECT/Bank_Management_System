#include "service/TransactionService.h"

#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <utility>

#include "infra/InputValidator.h"

namespace bms::service {

using domain::Account;
using domain::TxnState;
using domain::TxnType;

namespace {

// 125050 -> "1250.50"
std::string formatPaise(long long paise) {
    const bool negative = paise < 0;
    const unsigned long long v = negative
        ? 0ULL - static_cast<unsigned long long>(paise)
        : static_cast<unsigned long long>(paise);

    std::ostringstream out;
    if (negative) out << '-';
    out << (v / 100) << '.'
        << std::setw(2) << std::setfill('0') << (v % 100);
    return out.str();
}

} // namespace

const char* toString(TxnStatus status) {
    switch (status) {
        case TxnStatus::OK: return "OK";
        case TxnStatus::INVALID_INPUT: return "INVALID_INPUT";
        case TxnStatus::NOT_FOUND: return "NOT_FOUND";
        case TxnStatus::ACCOUNT_CLOSED: return "ACCOUNT_CLOSED";
        case TxnStatus::INSUFFICIENT_FUNDS:
            return "INSUFFICIENT_FUNDS";
        case TxnStatus::LIMIT_EXCEEDED: return "LIMIT_EXCEEDED";
        case TxnStatus::STORAGE_FAILURE: return "STORAGE_FAILURE";
    }
    return "STORAGE_FAILURE";
}

TransactionService::TransactionService(
    infra::AccountRepository& accounts,
    infra::TransactionRepository& txns,
    const infra::Config& config,
    const infra::AuditLogger& audit,
    Clock clock,
    infra::Journal* journal
)
    : accounts_(accounts),
      txns_(txns),
      config_(config),
      audit_(audit),
      clock_(std::move(clock)),
      journal_(journal) {}

TxnResult TransactionService::fail(
    TxnStatus status,
    const std::string& event,
    const std::string& accountNo,
    long long amountPaise
) const {
    audit_.log(
        event,
        accountNo,
        std::string("FAILED_") + toString(status),
        "amountPaise=" + std::to_string(amountPaise)
    );

    TxnResult result;
    result.status = status;
    return result;
}

TxnResult TransactionService::deposit(
    const std::string& accountNo,
    long long amountPaise,
    DepositMode mode
) {
    if (amountPaise <= 0 ||
        amountPaise > infra::kMaxAmountPaise) {
        return fail(
            TxnStatus::INVALID_INPUT,
            "DEPOSIT", accountNo, amountPaise
        );
    }

    const Account* account = accounts_.find(accountNo);

    if (!account) {
        return fail(
            TxnStatus::NOT_FOUND,
            "DEPOSIT", accountNo, amountPaise
        );
    }

    if (account->status == "CLOSED") {
        return fail(
            TxnStatus::ACCOUNT_CLOSED,
            "DEPOSIT", accountNo, amountPaise
        );
    }

    if (amountPaise >
        std::numeric_limits<long long>::max() -
        account->balancePaise) {
        return fail(
            TxnStatus::INVALID_INPUT,
            "DEPOSIT", accountNo, amountPaise
        );
    }

    Account updated = *account;
    updated.balancePaise += amountPaise;

    domain::Transaction txn;
    txn.type = TxnType::DEPOSIT;
    txn.accountNo = accountNo;
    txn.counterparty =
        mode == DepositMode::CASH ? "CASH" : "CHEQUE";
    txn.amountPaise = amountPaise;

    return finish(updated, txn);
}

TxnResult TransactionService::withdraw(
    const std::string& accountNo,
    long long amountPaise
) {
    if (amountPaise <= 0 ||
        amountPaise > infra::kMaxAmountPaise) {
        return fail(
            TxnStatus::INVALID_INPUT,
            "WITHDRAWAL", accountNo, amountPaise
        );
    }

    const Account* account = accounts_.find(accountNo);

    if (!account) {
        return fail(
            TxnStatus::NOT_FOUND,
            "WITHDRAWAL", accountNo, amountPaise
        );
    }

    if (account->status == "CLOSED") {
        return fail(
            TxnStatus::ACCOUNT_CLOSED,
            "WITHDRAWAL", accountNo, amountPaise
        );
    }

    if (amountPaise > account->balancePaise) {
        return fail(
            TxnStatus::INSUFFICIENT_FUNDS,
            "WITHDRAWAL", accountNo, amountPaise
        );
    }

    const std::string today = clock_().substr(0, 10);
    const long long alreadyToday =
        account->lastWithdrawDate == today
            ? account->withdrawnTodayPaise
            : 0;

    if (amountPaise >
        config_.dailyWithdrawalLimitPaise() - alreadyToday) {
        return fail(
            TxnStatus::LIMIT_EXCEEDED,
            "WITHDRAWAL", accountNo, amountPaise
        );
    }

    Account updated = *account;
    updated.balancePaise -= amountPaise;
    updated.withdrawnTodayPaise = alreadyToday + amountPaise;
    updated.lastWithdrawDate = today;

    domain::Transaction txn;
    txn.type = TxnType::WITHDRAWAL;
    txn.accountNo = accountNo;
    txn.counterparty = "-";
    txn.amountPaise = amountPaise;

    return finish(updated, txn);
}

TxnResult TransactionService::getBalance(
    const std::string& accountNo
) const {
    TxnResult result;
    const Account* account = accounts_.find(accountNo);

    if (!account) {
        result.status = TxnStatus::NOT_FOUND;
        return result;
    }

    result.status = TxnStatus::OK;
    result.newBalancePaise = account->balancePaise;
    return result;
}
//Issue 32
TxnStatus TransactionService::validateTransferAccounts(
    const std::string& sourceAccountNo,
    const std::string& destinationAccountNo
) const {
    const auto reject = [&](TxnStatus status,
                            const std::string& reason) {
        audit_.log(
            "TRANSFER_VALIDATION",
            sourceAccountNo,
            std::string("FAILED_") + toString(status),
            reason + " destination=" + destinationAccountNo
        );
        return status;
    };

    if (sourceAccountNo.empty() || destinationAccountNo.empty()) {
        return reject(TxnStatus::INVALID_INPUT, "EMPTY_ACCOUNT_NO");
    }

    if (sourceAccountNo == destinationAccountNo) {
        return reject(TxnStatus::INVALID_INPUT, "SAME_ACCOUNT");
    }

    const Account* source = accounts_.find(sourceAccountNo);
    if (!source) {
        return reject(TxnStatus::NOT_FOUND, "SOURCE_NOT_FOUND");
    }

    const Account* destination = accounts_.find(destinationAccountNo);
    if (!destination) {
        return reject(TxnStatus::NOT_FOUND, "DESTINATION_NOT_FOUND");
    }

    if (source->status == "CLOSED") {
        return reject(TxnStatus::ACCOUNT_CLOSED, "SOURCE_CLOSED");
    }

    if (destination->status == "CLOSED") {
        return reject(TxnStatus::ACCOUNT_CLOSED, "DESTINATION_CLOSED");
    }

    return TxnStatus::OK;
}

//Issue 33
TxnStatus TransactionService::validateTransferAmount(
    const std::string& sourceAccountNo,
    long long amountPaise
) const {
    const auto reject = [&](TxnStatus status,
                            const std::string& reason) {
        audit_.log(
            "TRANSFER_VALIDATION",
            sourceAccountNo,
            std::string("FAILED_") + toString(status),
            reason + " amountPaise=" + std::to_string(amountPaise)
        );
        return status;
    };

    if (amountPaise <= 0 || amountPaise > infra::kMaxAmountPaise) {
        return reject(TxnStatus::INVALID_INPUT, "INVALID_AMOUNT");
    }

    const Account* source = accounts_.find(sourceAccountNo);
    if (!source) {
        return reject(TxnStatus::NOT_FOUND, "SOURCE_NOT_FOUND");
    }

    if (source->status == "CLOSED") {
        return reject(TxnStatus::ACCOUNT_CLOSED, "SOURCE_CLOSED");
    }

    if (amountPaise > source->balancePaise) {
        return reject(TxnStatus::INSUFFICIENT_FUNDS, "INSUFFICIENT_FUNDS");
    }

    return TxnStatus::OK;
}

//Issue 34
TxnResult TransactionService::transfer(
    const std::string& sourceAccountNo,
    const std::string& destinationAccountNo,
    long long amountPaise
) {
    const TxnStatus accountsStatus =
        validateTransferAccounts(sourceAccountNo, destinationAccountNo);

    if (accountsStatus != TxnStatus::OK) {
        return fail(
            accountsStatus, "TRANSFER", sourceAccountNo, amountPaise
        );
    }

    const TxnStatus amountStatus =
        validateTransferAmount(sourceAccountNo, amountPaise);

    if (amountStatus != TxnStatus::OK) {
        return fail(
            amountStatus, "TRANSFER", sourceAccountNo, amountPaise
        );
    }

    // --- Leg 1: debit the source account ---
    const Account* source = accounts_.find(sourceAccountNo);

    Account debited = *source;
    debited.balancePaise -= amountPaise;

    domain::Transaction debitTxn;
    debitTxn.type = TxnType::WITHDRAWAL;
    debitTxn.accountNo = sourceAccountNo;
    debitTxn.counterparty = destinationAccountNo;
    debitTxn.amountPaise = amountPaise;

    const TxnResult debitResult = finish(debited, debitTxn);

    if (debitResult.status != TxnStatus::OK) {
        return debitResult;
    }

    // --- Leg 2: credit the destination account ---
    const Account* destination = accounts_.find(destinationAccountNo);

    if (!destination) {
        return fail(
            TxnStatus::NOT_FOUND, "TRANSFER",
            destinationAccountNo, amountPaise
        );
    }

    Account credited = *destination;
    credited.balancePaise += amountPaise;

    domain::Transaction creditTxn;
    creditTxn.type = TxnType::DEPOSIT;
    creditTxn.accountNo = destinationAccountNo;
    creditTxn.counterparty = sourceAccountNo;
    creditTxn.amountPaise = amountPaise;

    const TxnResult creditResult = finish(credited, creditTxn);

    if (creditResult.status != TxnStatus::OK) {
        // Credit failed after debit succeeded (e.g. storage
        // failure) — reverse the debit so funds aren't lost.
        const Account* currentSource =
            accounts_.find(sourceAccountNo);

        bool reversalSucceeded = false;

        if (currentSource) {
            Account restored = *currentSource;
            restored.balancePaise += amountPaise;

            domain::Transaction reversalTxn;
            reversalTxn.type = TxnType::DEPOSIT;
            reversalTxn.accountNo = sourceAccountNo;
            reversalTxn.counterparty = "TRANSFER_REVERSAL";
            reversalTxn.amountPaise = amountPaise;

            const TxnResult reversalResult =
                finish(restored, reversalTxn);
            reversalSucceeded =
                reversalResult.status == TxnStatus::OK;
        }

        if (reversalSucceeded) {
            audit_.log(
                "TRANSFER", sourceAccountNo,
                "FAILED_" +
                    std::string(toString(creditResult.status)),
                "destination=" + destinationAccountNo +
                    " amountPaise=" + std::to_string(amountPaise) +
                    " rolledBack=true"
            );
        } else {
            // The reversal itself failed: the source is left
            // debited with no matching credit anywhere. This
            // cannot be auto-healed further and needs a human.
            audit_.log(
                "TRANSFER_RECONCILIATION_REQUIRED",
                sourceAccountNo,
                "FAILED_" +
                    std::string(toString(creditResult.status)),
                "destination=" + destinationAccountNo +
                    " amountPaise=" + std::to_string(amountPaise) +
                    " rolledBack=false"
            );
        }

        TxnResult result;
        result.status = creditResult.status;
        return result;
    }

    audit_.log(
        "TRANSFER", sourceAccountNo, "SUCCESS",
        "destination=" + destinationAccountNo +
            " amountPaise=" + std::to_string(amountPaise) +
            " debitTxn=" + debitResult.txnId +
            " creditTxn=" + creditResult.txnId
    );

    TxnResult result;
    result.status = TxnStatus::OK;
    result.txnId = debitResult.txnId;
    result.newBalancePaise = debitResult.newBalancePaise;
    return result;
}

HistoryResult TransactionService::getHistory(
    const std::string& accountNo
) const {
    HistoryResult result;

    if (!accounts_.find(accountNo)) {
        result.status = TxnStatus::NOT_FOUND;
        return result;
    }

    // Every transaction is written to transactions.dat more than once
    // (PENDING, then COMMITTED or ABORTED). Only the latest row per
    // txnId describes its real outcome.
    const auto& all = txns_.all();
    std::map<std::string, std::size_t> latest;
    for (std::size_t i = 0; i < all.size(); ++i) {
        latest[all[i].txnId] = i;
    }

    // Newest first.
    for (std::size_t i = all.size(); i-- > 0;) {
        const auto& t = all[i];

        if (latest[t.txnId] != i) continue;
        if (t.state != TxnState::COMMITTED) continue;
        if (t.accountNo != accountNo) continue;

        result.records.push_back(t);
    }

    result.status = TxnStatus::OK;
    return result;
}

std::string TransactionService::formatHistory(
    const std::vector<domain::Transaction>& records
) {
    if (records.empty()) {
        return "No transactions found.\n";
    }

    std::ostringstream out;

    out << std::left
        << std::setw(11) << "TXN ID"
        << std::setw(21) << "DATE/TIME"
        << std::setw(12) << "TYPE"
        << std::setw(14) << "COUNTERPARTY"
        << std::right
        << std::setw(14) << "AMOUNT"
        << std::setw(16) << "BALANCE"
        << '\n';

    for (const auto& t : records) {
        out << std::left
            << std::setw(11) << t.txnId
            << std::setw(21) << t.timestamp
            << std::setw(12) << domain::toString(t.type)
            << std::setw(14) << t.counterparty
            << std::right
            << std::setw(14) << formatPaise(t.amountPaise)
            << std::setw(16) << formatPaise(t.balanceAfterPaise)
            << '\n';
    }

    return out.str();
}

TxnResult TransactionService::finish(
    const Account& updated,
    domain::Transaction txn
) {
    const Account* current = accounts_.find(updated.accountNo);
    if (!current) {
        return fail(
            TxnStatus::NOT_FOUND,
            "TRANSACTION", updated.accountNo, txn.amountPaise
        );
    }

    const Account previous = *current;

    txn.txnId = txns_.nextTxnId();
    txn.timestamp = clock_();
    txn.balanceAfterPaise = updated.balancePaise;
    txn.state = TxnState::PENDING;

    std::string jrnId;
    if (journal_) {
        jrnId = journal_->nextJournalId();
        domain::JournalEntry jPending{
            jrnId,
            txn.txnId,
            domain::toString(txn.type),
            txn.counterparty,
            updated.accountNo,
            txn.amountPaise,
            domain::JournalState::PENDING,
            txn.timestamp
        };
        if (!journal_->append(jPending)) {
            return fail(
                TxnStatus::STORAGE_FAILURE,
                domain::toString(txn.type),
                updated.accountNo, txn.amountPaise
            );
        }
    }

    if (!txns_.append(txn)) {
        if (journal_) {
            domain::JournalEntry jAbort{
                jrnId, txn.txnId, domain::toString(txn.type),
                txn.counterparty, updated.accountNo, txn.amountPaise,
                domain::JournalState::ABORTED, clock_()
            };
            journal_->append(jAbort);
        }
        return fail(
            TxnStatus::STORAGE_FAILURE,
            domain::toString(txn.type),
            updated.accountNo, txn.amountPaise
        );
    }

    // Step 2: Apply account balance changes in memory
    accounts_.upsert(updated);

    // Step 3: Write APPLIED journal entry
    if (journal_) {
        domain::JournalEntry jApplied{
            jrnId, txn.txnId, domain::toString(txn.type),
            txn.counterparty, updated.accountNo, txn.amountPaise,
            domain::JournalState::APPLIED, clock_()
        };
        journal_->append(jApplied);
    }

    // Step 4: Persist accounts (atomic save)
    if (!accounts_.save()) {
        accounts_.upsert(previous);

        if (journal_) {
            domain::JournalEntry jAbort{
                jrnId, txn.txnId, domain::toString(txn.type),
                txn.counterparty, updated.accountNo, txn.amountPaise,
                domain::JournalState::ABORTED, clock_()
            };
            journal_->append(jAbort);
        }

        txn.state = TxnState::ABORTED;
        txns_.append(txn);

        return fail(
            TxnStatus::STORAGE_FAILURE,
            domain::toString(txn.type),
            updated.accountNo, txn.amountPaise
        );
    }

    // Step 5: Write transaction record (COMMITTED)
    txn.state = TxnState::COMMITTED;

    if (!txns_.append(txn)) {
        audit_.log(
            "COMMIT_MARK_FAILED",
            updated.accountNo,
            "WARNING",
            txn.txnId
        );
    }

    // Step 6: Write COMMITTED journal entry
    if (journal_) {
        domain::JournalEntry jCommitted{
            jrnId, txn.txnId, domain::toString(txn.type),
            txn.counterparty, updated.accountNo, txn.amountPaise,
            domain::JournalState::COMMITTED, clock_()
        };
        journal_->append(jCommitted);
    }

    audit_.log(
        domain::toString(txn.type),
        updated.accountNo,
        "SUCCESS",
        txn.txnId + " amountPaise=" +
            std::to_string(txn.amountPaise)
    );

    TxnResult result;
    result.status = TxnStatus::OK;
    result.txnId = txn.txnId;
    result.newBalancePaise = updated.balancePaise;
    return result;
}

} // namespace bms::service
