// Issues #25-#31: deposit, withdrawal, limits, balance inquiry,
// and transaction records.

#include "infra/InputValidator.h"
#include "service/TransactionService.h"
#include "TestHarness.h"
#include "TestUtil.h"

#include <filesystem>
#include <string>

using namespace bms::infra;
using namespace bms::service;

using bms::domain::Account;
using bms::domain::TxnState;
using bms::domain::TxnType;
using bms::test::TempDir;

namespace {

// Everything uses temporary files and a controllable clock.
struct Fixture {
    TempDir dir;

    AccountRepository accounts{dir.file("accounts.dat")};
    TransactionRepository txns{dir.file("transactions.dat")};
    Config config;
    AuditLogger audit{dir.file("audit.log")};

    std::string now = "2026-10-23 10:00:00";

    TransactionService svc{
        accounts,
        txns,
        config,
        audit,
        [this] { return now; }
    };

    explicit Fixture(long long balancePaise = 100000) {
        Account account;
        account.accountNo = "BMS1001";
        account.customerId = "cust1001";
        account.balancePaise = balancePaise;

        accounts.upsert(account);
        accounts.save();
    }

    long long balance() {
        const auto result = svc.getBalance("BMS1001");
        return result.newBalancePaise;
    }

    long long balanceOnDisk() {
        AccountRepository repo(dir.file("accounts.dat"));

        if (repo.load() != LoadStatus::OK) {
            return -1;
        }

        const auto* account = repo.find("BMS1001");
        return account ? account->balancePaise : -1;
    }
};

} // namespace

// ------------------------------------------------------------
// Issue #25: Deposit operation
// ------------------------------------------------------------

static void test_deposit_updates_balance_in_memory_and_file() {
    Fixture f;

    const auto result =
        f.svc.deposit("BMS1001", 25050, DepositMode::CASH);

    CHECK(result.status == TxnStatus::OK);
    CHECK_EQ(result.newBalancePaise, 125050);
    CHECK_EQ(f.balance(), 125050);
    CHECK_EQ(f.balanceOnDisk(), 125050);
    CHECK_EQ(result.txnId, "TXN000001");
}

static void test_deposit_unknown_and_closed_account() {
    Fixture f;

    CHECK(
        f.svc.deposit("NOPE", 100, DepositMode::CASH).status
        == TxnStatus::NOT_FOUND
    );

    // The team's Account.h represents status as a string.
    f.accounts.find("BMS1001")->status = "CLOSED";

    CHECK(
        f.svc.deposit("BMS1001", 100, DepositMode::CASH).status
        == TxnStatus::ACCOUNT_CLOSED
    );

    CHECK_EQ(f.balance(), 100000);
    CHECK_EQ(f.balanceOnDisk(), 100000);
}

static void test_deposit_smallest_and_largest_valid() {
    Fixture f(0);

    const auto smallest =
        f.svc.deposit("BMS1001", 1, DepositMode::CASH);

    CHECK(smallest.status == TxnStatus::OK);
    CHECK_EQ(f.balance(), 1);

    const auto largest =
        f.svc.deposit(
            "BMS1001", kMaxAmountPaise, DepositMode::CHEQUE
        );

    CHECK(largest.status == TxnStatus::OK);
    CHECK_EQ(f.balance(), kMaxAmountPaise + 1);
    CHECK_EQ(f.balanceOnDisk(), kMaxAmountPaise + 1);
}

// ------------------------------------------------------------
// Issue #26: Invalid deposit amounts
// ------------------------------------------------------------

static void test_invalid_deposit_amounts_rejected_balance_unchanged() {
    Fixture f;

    CHECK(
        f.svc.deposit("BMS1001", 0, DepositMode::CASH).status
        == TxnStatus::INVALID_INPUT
    );

    CHECK(
        f.svc.deposit("BMS1001", -1, DepositMode::CASH).status
        == TxnStatus::INVALID_INPUT
    );

    CHECK(
        f.svc.deposit(
            "BMS1001",
            kMaxAmountPaise + 1,
            DepositMode::CASH
        ).status == TxnStatus::INVALID_INPUT
    );

    CHECK_EQ(f.balance(), 100000);
    CHECK_EQ(f.balanceOnDisk(), 100000);
    CHECK_EQ(f.txns.all().size(), 0u);
}

// ------------------------------------------------------------
// Issue #27: Withdrawal operation
// ------------------------------------------------------------

static void test_withdraw_reduces_balance() {
    Fixture f;

    const auto result = f.svc.withdraw("BMS1001", 30000);

    CHECK(result.status == TxnStatus::OK);
    CHECK_EQ(result.newBalancePaise, 70000);
    CHECK_EQ(f.balanceOnDisk(), 70000);
}

static void test_withdraw_invalid_amounts_and_accounts() {
    Fixture f;

    CHECK(
        f.svc.withdraw("BMS1001", 0).status
        == TxnStatus::INVALID_INPUT
    );

    CHECK(
        f.svc.withdraw("BMS1001", -500).status
        == TxnStatus::INVALID_INPUT
    );

    CHECK(
        f.svc.withdraw("NOPE", 100).status
        == TxnStatus::NOT_FOUND
    );

    f.accounts.find("BMS1001")->status = "CLOSED";

    CHECK(
        f.svc.withdraw("BMS1001", 100).status
        == TxnStatus::ACCOUNT_CLOSED
    );

    CHECK_EQ(f.balance(), 100000);
}

// ------------------------------------------------------------
// Issue #28: Available-balance checks
// ------------------------------------------------------------

static void test_withdraw_exact_balance_succeeds() {
    Fixture f;

    const auto result = f.svc.withdraw("BMS1001", 100000);

    CHECK(result.status == TxnStatus::OK);
    CHECK_EQ(f.balance(), 0);
}

static void test_withdraw_one_paisa_over_balance_fails() {
    Fixture f;

    const auto result = f.svc.withdraw("BMS1001", 100001);

    CHECK(result.status == TxnStatus::INSUFFICIENT_FUNDS);
    CHECK_EQ(f.balance(), 100000);
    CHECK_EQ(f.balanceOnDisk(), 100000);
    CHECK_EQ(f.txns.all().size(), 0u);
}

// ------------------------------------------------------------
// Issue #29: Daily withdrawal limits
// ------------------------------------------------------------

static void test_daily_limit_boundary_single_withdrawal() {
    Fixture f(10000000);

    f.config.setDailyWithdrawalLimitPaise(500000);

    CHECK(
        f.svc.withdraw("BMS1001", 500001).status
        == TxnStatus::LIMIT_EXCEEDED
    );

    CHECK_EQ(f.balance(), 10000000);

    CHECK(
        f.svc.withdraw("BMS1001", 500000).status
        == TxnStatus::OK
    );
}

static void test_daily_limit_accumulates() {
    Fixture f(10000000);

    f.config.setDailyWithdrawalLimitPaise(500000);

    CHECK(
        f.svc.withdraw("BMS1001", 499900).status
        == TxnStatus::OK
    );

    CHECK(
        f.svc.withdraw("BMS1001", 100).status
        == TxnStatus::OK
    );

    CHECK(
        f.svc.withdraw("BMS1001", 1).status
        == TxnStatus::LIMIT_EXCEEDED
    );

    CHECK_EQ(
        f.accounts.find("BMS1001")->withdrawnTodayPaise,
        500000
    );
}

static void test_daily_limit_resets_next_day_and_survives_reload() {
    Fixture f(10000000);

    f.config.setDailyWithdrawalLimitPaise(500000);

    CHECK(
        f.svc.withdraw("BMS1001", 500000).status
        == TxnStatus::OK
    );

    AccountRepository reloaded(f.dir.file("accounts.dat"));

    CHECK(reloaded.load() == LoadStatus::OK);

    TransactionService later(
        reloaded,
        f.txns,
        f.config,
        f.audit,
        [&f] { return f.now; }
    );

    CHECK(
        later.withdraw("BMS1001", 1).status
        == TxnStatus::LIMIT_EXCEEDED
    );

    f.now = "2026-10-24 09:00:00";

    CHECK(
        later.withdraw("BMS1001", 100).status
        == TxnStatus::OK
    );
}

static void test_limit_uses_config_value() {
    Fixture f(10000000);

    CHECK(
        f.svc.withdraw("BMS1001", 2500000).status
        == TxnStatus::OK
    );

    CHECK(
        f.svc.withdraw("BMS1001", 1).status
        == TxnStatus::LIMIT_EXCEEDED
    );
}

static void test_insufficient_funds_reported_before_limit() {
    Fixture f(1000);

    f.config.setDailyWithdrawalLimitPaise(500);

    CHECK(
        f.svc.withdraw("BMS1001", 2000).status
        == TxnStatus::INSUFFICIENT_FUNDS
    );
}

// ------------------------------------------------------------
// Issue #30: Balance inquiry
// ------------------------------------------------------------

static void test_balance_inquiry() {
    Fixture f(123456);

    const auto result = f.svc.getBalance("BMS1001");

    CHECK(result.status == TxnStatus::OK);
    CHECK_EQ(result.newBalancePaise, 123456);

    CHECK(
        f.svc.getBalance("NOPE").status
        == TxnStatus::NOT_FOUND
    );
}

static void test_balance_after_mixed_operations() {
    Fixture f(100000);

    CHECK(
        f.svc.deposit("BMS1001", 5000, DepositMode::CASH).status
        == TxnStatus::OK
    );

    CHECK(
        f.svc.withdraw("BMS1001", 20000).status
        == TxnStatus::OK
    );

    CHECK(
        f.svc.deposit("BMS1001", 1, DepositMode::CHEQUE).status
        == TxnStatus::OK
    );

    CHECK_EQ(f.balance(), 85001);
    CHECK_EQ(f.balanceOnDisk(), 85001);
}

// ------------------------------------------------------------
// Issue #31: Transaction records and journal
// ------------------------------------------------------------

static void test_successful_deposit_writes_journal_and_history() {
    Fixture f;

    const auto result =
        f.svc.deposit("BMS1001", 2550, DepositMode::CHEQUE);

    CHECK(result.status == TxnStatus::OK);

    const auto& all = f.txns.all();

    CHECK_EQ(all.size(), 2u);
    CHECK(all[0].state == TxnState::PENDING);
    CHECK(all[1].state == TxnState::COMMITTED);
    CHECK_EQ(all[1].txnId, "TXN000001");
    CHECK(all[1].type == TxnType::DEPOSIT);
    CHECK_EQ(all[1].accountNo, "BMS1001");
    CHECK_EQ(all[1].counterparty, "CHEQUE");
    CHECK_EQ(all[1].amountPaise, 2550);
    CHECK_EQ(all[1].balanceAfterPaise, 102550);
    CHECK_EQ(all[1].timestamp, "2026-10-23 10:00:00");
    CHECK(f.txns.pending().empty());
}

static void test_withdrawal_record_and_unique_ids() {
    Fixture f;

    const auto first = f.svc.withdraw("BMS1001", 100);
    const auto second = f.svc.withdraw("BMS1001", 200);

    CHECK(first.status == TxnStatus::OK);
    CHECK(second.status == TxnStatus::OK);
    CHECK(first.txnId != second.txnId);
    CHECK_EQ(second.txnId, "TXN000002");
    CHECK(f.txns.all()[1].type == TxnType::WITHDRAWAL);
    CHECK_EQ(f.txns.all()[1].counterparty, "-");

    TransactionRepository reloaded(
        f.dir.file("transactions.dat")
    );

    CHECK(reloaded.load() == LoadStatus::OK);
    CHECK_EQ(reloaded.all().size(), 4u);
}

static void test_deposit_mode_cash_is_recorded() {
    Fixture f;

    const auto result =
        f.svc.deposit("BMS1001", 100, DepositMode::CASH);

    CHECK(result.status == TxnStatus::OK);
    CHECK_EQ(f.txns.all()[1].counterparty, "CASH");
}

// ------------------------------------------------------------
// Storage failure: the account balance must remain unchanged
// ------------------------------------------------------------

static void test_storage_failure_rolls_back() {
    Fixture f;

    // Force the atomic account-file write to fail.
    std::filesystem::create_directory(
        f.dir.file("accounts.dat.tmp")
    );

    const auto result =
        f.svc.deposit("BMS1001", 5000, DepositMode::CASH);

    CHECK(result.status == TxnStatus::STORAGE_FAILURE);
    CHECK_EQ(f.balance(), 100000);
    CHECK_EQ(f.balanceOnDisk(), 100000);
    CHECK(f.txns.pending().empty());
    CHECK(!f.txns.all().empty());
    CHECK(f.txns.all().back().state == TxnState::ABORTED);
}

static void test_audit_log_records_success_and_failure() {
    Fixture f;

    const auto deposit =
        f.svc.deposit("BMS1001", 100, DepositMode::CASH);

    const auto withdrawal =
        f.svc.withdraw("BMS1001", 999999999);

    CHECK(deposit.status == TxnStatus::OK);
    CHECK(withdrawal.status == TxnStatus::INSUFFICIENT_FUNDS);

    const std::string log =
        bms::test::readFile(f.dir.file("audit.log"));

    CHECK(
        log.find("|DEPOSIT|BMS1001|SUCCESS|")
        != std::string::npos
    );

    CHECK(
        log.find("|WITHDRAWAL|BMS1001|FAILED_INSUFFICIENT_FUNDS|")
        != std::string::npos
    );
}

int main() {
    test_deposit_updates_balance_in_memory_and_file();
    test_deposit_unknown_and_closed_account();
    test_deposit_smallest_and_largest_valid();
    test_invalid_deposit_amounts_rejected_balance_unchanged();

    test_withdraw_reduces_balance();
    test_withdraw_invalid_amounts_and_accounts();
    test_withdraw_exact_balance_succeeds();
    test_withdraw_one_paisa_over_balance_fails();

    test_daily_limit_boundary_single_withdrawal();
    test_daily_limit_accumulates();
    test_daily_limit_resets_next_day_and_survives_reload();
    test_limit_uses_config_value();
    test_insufficient_funds_reported_before_limit();

    test_balance_inquiry();
    test_balance_after_mixed_operations();

    test_successful_deposit_writes_journal_and_history();
    test_withdrawal_record_and_unique_ids();
    test_deposit_mode_cash_is_recorded();

    test_storage_failure_rolls_back();
    test_audit_log_records_success_and_failure();

    return bms::test::summary("test_transaction_service");
}