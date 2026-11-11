// Issue #37: display transaction history.

#include "infra/InputValidator.h"
#include "service/TransactionService.h"
#include "TestHarness.h"
#include "TestUtil.h"

#include <filesystem>
#include <string>

using namespace bms::infra;
using namespace bms::service;

using bms::domain::Account;
using bms::domain::TxnType;
using bms::test::TempDir;

namespace {

struct Fixture {
    TempDir dir;

    AccountRepository accounts{dir.file("accounts.dat")};
    TransactionRepository txns{dir.file("transactions.dat")};
    Config config;
    AuditLogger audit{dir.file("audit.log")};

    std::string now = "2026-10-23 10:00:00";

    TransactionService svc{
        accounts, txns, config, audit,
        [this] { return now; }
    };

    Fixture() {
        addAccount("BMS1001", "cust1001", 100000);
        addAccount("BMS1002", "cust1002", 50000);
    }

    void addAccount(const std::string& no, const std::string& cust,
                    long long balance) {
        Account a;
        a.accountNo = no;
        a.customerId = cust;
        a.balancePaise = balance;
        accounts.upsert(a);
        accounts.save();
    }
};

} // namespace

static void test_history_lists_committed_transactions_newest_first() {
    Fixture f;

    f.now = "2026-10-23 10:00:00";
    CHECK(f.svc.deposit("BMS1001", 5000, DepositMode::CASH).status
          == TxnStatus::OK);

    f.now = "2026-10-23 11:00:00";
    CHECK(f.svc.withdraw("BMS1001", 2000).status == TxnStatus::OK);

    const auto h = f.svc.getHistory("BMS1001");

    CHECK(h.status == TxnStatus::OK);
    CHECK_EQ(h.records.size(), 2u);

    if (h.records.size() == 2) {
        CHECK_EQ(h.records[0].txnId, "TXN000002");
        CHECK(h.records[0].type == TxnType::WITHDRAWAL);
        CHECK_EQ(h.records[0].balanceAfterPaise, 103000);

        CHECK_EQ(h.records[1].txnId, "TXN000001");
        CHECK(h.records[1].type == TxnType::DEPOSIT);
        CHECK_EQ(h.records[1].balanceAfterPaise, 105000);
    }
}

static void test_history_shows_one_row_per_transaction() {
    Fixture f;

    CHECK(f.svc.deposit("BMS1001", 100, DepositMode::CASH).status
          == TxnStatus::OK);

    // transactions.dat holds PENDING + COMMITTED for it.
    CHECK_EQ(f.txns.all().size(), 2u);
    CHECK_EQ(f.svc.getHistory("BMS1001").records.size(), 1u);
}

static void test_history_only_includes_requested_account() {
    Fixture f;

    CHECK(f.svc.deposit("BMS1001", 100, DepositMode::CASH).status
          == TxnStatus::OK);
    CHECK(f.svc.deposit("BMS1002", 200, DepositMode::CHEQUE).status
          == TxnStatus::OK);

    const auto h1 = f.svc.getHistory("BMS1001");
    const auto h2 = f.svc.getHistory("BMS1002");

    CHECK_EQ(h1.records.size(), 1u);
    CHECK_EQ(h2.records.size(), 1u);

    if (!h1.records.empty() && !h2.records.empty()) {
        CHECK_EQ(h1.records[0].amountPaise, 100);
        CHECK_EQ(h2.records[0].amountPaise, 200);
    }
}

static void test_history_excludes_failed_and_aborted_transactions() {
    Fixture f;

    // Rejected before anything is written.
    CHECK(f.svc.withdraw("BMS1001", 999999999).status
          == TxnStatus::INSUFFICIENT_FUNDS);

    // Aborted after PENDING was written: force the account save to fail.
    std::filesystem::create_directory(f.dir.file("accounts.dat.tmp"));
    CHECK(f.svc.deposit("BMS1001", 5000, DepositMode::CASH).status
          == TxnStatus::STORAGE_FAILURE);

    CHECK(!f.txns.all().empty());
    CHECK(f.svc.getHistory("BMS1001").records.empty());
}

static void test_history_empty_account_and_unknown_account() {
    Fixture f;

    const auto empty = f.svc.getHistory("BMS1001");
    CHECK(empty.status == TxnStatus::OK);
    CHECK(empty.records.empty());

    CHECK(f.svc.getHistory("NOPE").status == TxnStatus::NOT_FOUND);
}

static void test_history_survives_reload() {
    Fixture f;

    CHECK(f.svc.deposit("BMS1001", 2550, DepositMode::CHEQUE).status
          == TxnStatus::OK);

    AccountRepository accounts(f.dir.file("accounts.dat"));
    TransactionRepository txns(f.dir.file("transactions.dat"));
    CHECK(accounts.load().isOk());
    CHECK(txns.load().isOk());

    TransactionService svc(
        accounts, txns, f.config, f.audit, [&f] { return f.now; });

    const auto h = svc.getHistory("BMS1001");
    CHECK_EQ(h.records.size(), 1u);
    if (!h.records.empty()) {
        CHECK_EQ(h.records[0].amountPaise, 2550);
    }
}

static void test_format_history() {
    Fixture f;

    CHECK(TransactionService::formatHistory({}).find("No transactions")
          != std::string::npos);

    CHECK(f.svc.deposit("BMS1001", 125050, DepositMode::CHEQUE).status
          == TxnStatus::OK);

    const std::string text =
        TransactionService::formatHistory(
            f.svc.getHistory("BMS1001").records);

    CHECK(text.find("TXN000001") != std::string::npos);
    CHECK(text.find("DEPOSIT") != std::string::npos);
    CHECK(text.find("CHEQUE") != std::string::npos);
    CHECK(text.find("1250.50") != std::string::npos);    // amount
    CHECK(text.find("2250.50") != std::string::npos);    // balance after
    CHECK(text.find("2026-10-23 10:00:00") != std::string::npos);

    bms::domain::Transaction negative;
    negative.txnId = "TXN-NEGATIVE";
    negative.timestamp = "2026-10-23 10:00:00";
    negative.type = TxnType::DEPOSIT;
    negative.counterparty = "CASH";
    negative.amountPaise = -1;
    negative.balanceAfterPaise = -100;
    const std::string negativeText =
        TransactionService::formatHistory({negative});
    CHECK(negativeText.find("-0.01") != std::string::npos);
    CHECK(negativeText.find("-1.00") != std::string::npos);
}
// ------------------------------------------------------------
// Issue #38: filter by date or type
// ------------------------------------------------------------

// TXN000001 deposit 1000 on 10-22, TXN000002 withdrawal 500 on 10-23
// (23:59:59), TXN000003 deposit 2000 on 10-24 (00:00:00).
static void seedThreeDays(Fixture& f) {
    f.now = "2026-10-22 09:00:00";
    CHECK(f.svc.deposit("BMS1001", 1000, DepositMode::CASH).status
          == TxnStatus::OK);

    f.now = "2026-10-23 23:59:59";
    CHECK(f.svc.withdraw("BMS1001", 500).status == TxnStatus::OK);

    f.now = "2026-10-24 00:00:00";
    CHECK(f.svc.deposit("BMS1001", 2000, DepositMode::CHEQUE).status
          == TxnStatus::OK);
}

static void test_filter_by_type() {
    Fixture f;
    seedThreeDays(f);

    const auto deposits =
        f.svc.getHistory("BMS1001", "", "", TxnType::DEPOSIT);
    CHECK(deposits.status == TxnStatus::OK);
    CHECK_EQ(deposits.records.size(), 2u);
    for (const auto& t : deposits.records) {
        CHECK(t.type == TxnType::DEPOSIT);
    }

    const auto withdrawals =
        f.svc.getHistory("BMS1001", "", "", TxnType::WITHDRAWAL);
    CHECK_EQ(withdrawals.records.size(), 1u);
    if (!withdrawals.records.empty()) {
        CHECK_EQ(withdrawals.records[0].txnId, "TXN000002");
    }

    CHECK_EQ(f.svc.getHistory("BMS1001").records.size(), 3u);
}

static void test_filter_by_date_range_is_inclusive() {
    Fixture f;
    seedThreeDays(f);

    // One day only: includes 23:59:59, excludes 10-24 00:00:00.
    const auto oneDay =
        f.svc.getHistory("BMS1001", "2026-10-23", "2026-10-23");
    CHECK_EQ(oneDay.records.size(), 1u);
    if (!oneDay.records.empty()) {
        CHECK_EQ(oneDay.records[0].txnId, "TXN000002");
    }

    // From only: newest first.
    const auto fromOnly = f.svc.getHistory("BMS1001", "2026-10-23");
    CHECK_EQ(fromOnly.records.size(), 2u);
    if (fromOnly.records.size() == 2) {
        CHECK_EQ(fromOnly.records[0].txnId, "TXN000003");
        CHECK_EQ(fromOnly.records[1].txnId, "TXN000002");
    }

    // To only.
    const auto toOnly = f.svc.getHistory("BMS1001", "", "2026-10-23");
    CHECK_EQ(toOnly.records.size(), 2u);
}

static void test_filter_by_date_and_type_together() {
    Fixture f;
    seedThreeDays(f);

    const auto h =
        f.svc.getHistory("BMS1001", "2026-10-23", "", TxnType::DEPOSIT);
    CHECK_EQ(h.records.size(), 1u);
    if (!h.records.empty()) {
        CHECK_EQ(h.records[0].amountPaise, 2000);
    }
}

static void test_filter_with_no_match_is_ok_and_empty() {
    Fixture f;
    seedThreeDays(f);

    const auto h = f.svc.getHistory(
        "BMS1001", "2026-10-24", "", TxnType::WITHDRAWAL);
    CHECK(h.status == TxnStatus::OK);
    CHECK(h.records.empty());
}

static void test_filter_invalid_dates_rejected() {
    Fixture f;
    seedThreeDays(f);

    const std::vector<std::string> invalidDates = {
        "2026-13-01",
        "2026-00-01",
        "2026-02-30",
        "2026-04-31",
        "2026-02-00",
        "2026/10/22",
        "2026-10/22",
        "2026-1-01",
        "202x-10-22",
        "2026-0/22",
        "2026-:0-22",
        "0000-01-01",
        "1900-02-29"
    };
    for (const std::string& date : invalidDates) {
        CHECK(
            f.svc.getHistory("BMS1001", date).status
            == TxnStatus::INVALID_INPUT
        );
    }

    CHECK(f.svc.getHistory("BMS1001", "", "abc").status
          == TxnStatus::INVALID_INPUT);

    // from after to
    CHECK(f.svc.getHistory("BMS1001", "2026-10-24", "2026-10-22").status
          == TxnStatus::INVALID_INPUT);

    // Both ordinary leap years and century leap years are valid.
    CHECK(f.svc.getHistory("BMS1001", "2028-02-29").status
          == TxnStatus::OK);
    CHECK(f.svc.getHistory("BMS1001", "2000-02-29").status
          == TxnStatus::OK);
}
int main() {
    test_history_lists_committed_transactions_newest_first();
    test_history_shows_one_row_per_transaction();
    test_history_only_includes_requested_account();
    test_history_excludes_failed_and_aborted_transactions();
    test_history_empty_account_and_unknown_account();
    test_history_survives_reload();
    test_format_history();
    
    test_filter_by_type();
    test_filter_by_date_range_is_inclusive();
    test_filter_by_date_and_type_together();
    test_filter_with_no_match_is_ok_and_empty();
    test_filter_invalid_dates_rejected();
    return bms::test::summary("test_transaction_history");
}