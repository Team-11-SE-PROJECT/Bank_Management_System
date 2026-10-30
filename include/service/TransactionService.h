#pragma once

#include <functional>
#include <string>

#include "infra/AccountRepository.h"
#include "infra/AuditLogger.h"
#include "infra/Config.h"
#include "infra/Journal.h"
#include "infra/TimeUtil.h"
#include "infra/TransactionRepository.h"

namespace bms::service {

enum class TxnStatus {
    OK,
    INVALID_INPUT,
    NOT_FOUND,
    ACCOUNT_CLOSED,
    INSUFFICIENT_FUNDS,
    LIMIT_EXCEEDED,
    STORAGE_FAILURE
};

enum class DepositMode { CASH, CHEQUE };

struct TxnResult {
    TxnStatus status = TxnStatus::OK;
    std::string txnId;
    long long newBalancePaise = 0;
};

class TransactionService {
public:
    using Clock = std::function<std::string()>;

    TransactionService(
        infra::AccountRepository& accounts,
        infra::TransactionRepository& txns,
        const infra::Config& config,
        const infra::AuditLogger& audit,
        Clock clock = &infra::nowTimestamp,
        infra::Journal* journal = nullptr
    );

    void setJournal(infra::Journal* journal) {
        journal_ = journal;
    }

    TxnResult deposit(
        const std::string& accountNo,
        long long amountPaise,
        DepositMode mode
    );

    TxnResult withdraw(
        const std::string& accountNo,
        long long amountPaise
    );

    TxnResult getBalance(
        const std::string& accountNo
    ) const;

private:
    TxnResult finish(
        const domain::Account& updated,
        domain::Transaction txn
    );

    TxnResult fail(
        TxnStatus status,
        const std::string& event,
        const std::string& accountNo,
        long long amountPaise
    ) const;

    infra::AccountRepository& accounts_;
    infra::TransactionRepository& txns_;
    const infra::Config& config_;
    const infra::AuditLogger& audit_;
    Clock clock_;
    infra::Journal* journal_ = nullptr;
};

const char* toString(TxnStatus status);

} // namespace bms::service
