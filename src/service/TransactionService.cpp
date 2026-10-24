#include "service/TransactionService.h"

#include <limits>
#include <utility>

#include "infra/InputValidator.h"

namespace bms::service {

using domain::Account;
using domain::TxnState;
using domain::TxnType;

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
    Clock clock
)
    : accounts_(accounts),
      txns_(txns),
      config_(config),
      audit_(audit),
      clock_(std::move(clock)) {}

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

    if (!txns_.append(txn)) {
        return fail(
            TxnStatus::STORAGE_FAILURE,
            domain::toString(txn.type),
            updated.accountNo, txn.amountPaise
        );
    }

    accounts_.upsert(updated);

    if (!accounts_.save()) {
        accounts_.upsert(previous);

        txn.state = TxnState::ABORTED;
        txns_.append(txn);

        return fail(
            TxnStatus::STORAGE_FAILURE,
            domain::toString(txn.type),
            updated.accountNo, txn.amountPaise
        );
    }

    txn.state = TxnState::COMMITTED;

    if (!txns_.append(txn)) {
        audit_.log(
            "COMMIT_MARK_FAILED",
            updated.accountNo,
            "WARNING",
            txn.txnId
        );
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