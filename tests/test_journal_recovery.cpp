#include <filesystem>
#include <fstream>
#include <string>

#include "domain/Account.h"
#include "domain/JournalEntry.h"
#include "domain/Transaction.h"
#include "infra/AccountRepository.h"
#include "infra/AuditLogger.h"
#include "infra/Config.h"
#include "infra/DataFormat.h"
#include "infra/Journal.h"
#include "infra/TransactionRepository.h"
#include "service/TransactionService.h"
#include "TestHarness.h"

namespace fs = std::filesystem;
using namespace bms::infra;
using namespace bms::domain;
using namespace bms::service;

static const std::string kTestDir = "build-validate/test_journal_recovery_data";

static void cleanup() {
    std::error_code ec;
    fs::remove_all(kTestDir, ec);
    fs::create_directories(kTestDir, ec);
}

static void writeFile(const std::string& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary);
    out << content;
}

static void test_pending_crash_recovery() {
    cleanup();
    std::string accPath = kTestDir + "/accounts.dat";
    std::string txnPath = kTestDir + "/transactions.dat";
    std::string jrnPath = kTestDir + "/journal.dat";
    std::string auditPath = kTestDir + "/audit.log";

    AccountRepository accRepo(accPath);
    TransactionRepository txnRepo(txnPath);
    AuditLogger audit(auditPath);

    // Initial account balance: 100,000 paise (Rs. 1000.00)
    Account acc;
    acc.accountNo = "1000000001";
    acc.customerId = "CUST1001";
    acc.balancePaise = 100000;
    acc.status = "ACTIVE";
    accRepo.upsert(acc);
    accRepo.save();

    // Crash simulation: a PENDING entry was logged for a withdrawal of 30,000 paise,
    // but the system crashed BEFORE balance was deducted or applied.
    std::string line;
    std::string err;
    JournalEntry pendingEntry{
        "JRN00000001",
        "TXN00000001",
        "WITHDRAWAL",
        "-",
        "1000000001",
        30000,
        JournalState::PENDING,
        "2026-03-30 10:00:00"
    };
    CHECK(format::serializeJournalEntry(pendingEntry, line, err));

    writeFile(jrnPath, "#VERSION 1\n" + line + "\n");

    Journal journal(jrnPath);
    CHECK(journal.load().isOk());
    CHECK_EQ(journal.entries().size(), 1U);
    CHECK(journal.entries()[0].state == JournalState::PENDING);

    // Run recovery
    int recovered = Journal::recover(journal, accRepo, txnRepo, &audit);
    CHECK_EQ(recovered, 1);

    // Balance must NOT have been changed (remains 100000)
    const auto* recoveredAcc = accRepo.find("1000000001");
    CHECK(recoveredAcc != nullptr);
    if (recoveredAcc) {
        CHECK_EQ(recoveredAcc->balancePaise, 100000LL);
    }

    // No transaction should be recorded
    CHECK_EQ(txnRepo.all().size(), 0U);

    // Reload journal: verify entry was marked ABORTED
    Journal reloadedJournal(jrnPath);
    CHECK(reloadedJournal.load().isOk());
    CHECK_EQ(reloadedJournal.entries().size(), 2U);
    CHECK(reloadedJournal.entries().back().state == JournalState::ABORTED);
}

static void test_applied_crash_recovery_deposit() {
    cleanup();
    std::string accPath = kTestDir + "/accounts.dat";
    std::string txnPath = kTestDir + "/transactions.dat";
    std::string jrnPath = kTestDir + "/journal.dat";
    std::string auditPath = kTestDir + "/audit.log";

    AccountRepository accRepo(accPath);
    TransactionRepository txnRepo(txnPath);
    AuditLogger audit(auditPath);

    // Initial account on disk has balance 100,000 paise (Rs. 1000.00)
    Account acc;
    acc.accountNo = "1000000001";
    acc.customerId = "CUST1001";
    acc.balancePaise = 100000;
    acc.status = "ACTIVE";
    accRepo.upsert(acc);
    accRepo.save();

    // Crash simulation: a deposit of 50,000 paise reached APPLIED stage in journal,
    // but crashed before accounts.dat was saved or transactions.dat was written.
    std::string line;
    std::string err;
    JournalEntry appliedEntry{
        "JRN00000002",
        "TXN00000002",
        "DEPOSIT",
        "-",
        "1000000001",
        50000,
        JournalState::APPLIED,
        "2026-03-30 11:00:00"
    };
    CHECK(format::serializeJournalEntry(appliedEntry, line, err));

    writeFile(jrnPath, "#VERSION 1\n" + line + "\n");

    Journal journal(jrnPath);
    CHECK(journal.load().isOk());
    CHECK_EQ(journal.entries().size(), 1U);

    // Run recovery
    int recovered = Journal::recover(journal, accRepo, txnRepo, &audit);
    CHECK_EQ(recovered, 1);

    // Account balance should now be reconciled to 150000 paise
    AccountRepository reloadedAcc(accPath);
    CHECK(reloadedAcc.load().isOk());
    const auto* updatedAcc = reloadedAcc.find("1000000001");
    CHECK(updatedAcc != nullptr);
    if (updatedAcc) {
        CHECK_EQ(updatedAcc->balancePaise, 150000LL);
    }

    // Transaction should now be recorded in transactions repo
    TransactionRepository reloadedTxn(txnPath);
    CHECK(reloadedTxn.load().isOk());
    const auto& txns = reloadedTxn.all();
    CHECK_EQ(txns.size(), 1U);
    if (txns.size() == 1U) {
        CHECK_EQ(txns[0].txnId, "TXN00000002");
        CHECK_EQ(txns[0].amountPaise, 50000LL);
        CHECK(txns[0].type == TxnType::DEPOSIT);
        CHECK(txns[0].state == TxnState::COMMITTED);
    }

    // Journal should now reflect COMMITTED
    Journal reloadedJournal(jrnPath);
    CHECK(reloadedJournal.load().isOk());
    CHECK_EQ(reloadedJournal.entries().size(), 2U);
    CHECK(reloadedJournal.entries().back().state == JournalState::COMMITTED);
}

static void test_idempotent_recovery() {
    cleanup();
    std::string accPath = kTestDir + "/accounts.dat";
    std::string txnPath = kTestDir + "/transactions.dat";
    std::string jrnPath = kTestDir + "/journal.dat";
    std::string auditPath = kTestDir + "/audit.log";

    AccountRepository accRepo(accPath);
    TransactionRepository txnRepo(txnPath);
    AuditLogger audit(auditPath);

    Account acc;
    acc.accountNo = "1000000001";
    acc.customerId = "CUST1001";
    acc.balancePaise = 200000;
    acc.status = "ACTIVE";
    accRepo.upsert(acc);
    accRepo.save();

    Journal journal(jrnPath);
    journal.clearAndReset();

    Config cfg;
    TransactionService txnService(accRepo, txnRepo, cfg, audit);
    txnService.setJournal(&journal);

    // Perform standard deposit
    auto res = txnService.deposit("1000000001", 100000, DepositMode::CASH);
    CHECK_EQ(res.status, TxnStatus::OK);
    CHECK_EQ(res.newBalancePaise, 300000LL);

    // Running recovery now should find 0 uncommitted items and change nothing
    int rec1 = Journal::recover(journal, accRepo, txnRepo, &audit);
    CHECK_EQ(rec1, 0);

    const auto* accAfterRec1 = accRepo.find("1000000001");
    CHECK(accAfterRec1 != nullptr);
    if (accAfterRec1) {
        CHECK_EQ(accAfterRec1->balancePaise, 300000LL);
    }
    // 2 records in txns: PENDING and COMMITTED
    CHECK_EQ(txnRepo.all().size(), 2U);

    // Running recovery a second time should also do nothing
    int rec2 = Journal::recover(journal, accRepo, txnRepo, &audit);
    CHECK_EQ(rec2, 0);
    const auto* accAfterRec2 = accRepo.find("1000000001");
    CHECK(accAfterRec2 != nullptr);
    if (accAfterRec2) {
        CHECK_EQ(accAfterRec2->balancePaise, 300000LL);
    }
    CHECK_EQ(txnRepo.all().size(), 2U);
}

int main() {
    test_pending_crash_recovery();
    test_applied_crash_recovery_deposit();
    test_idempotent_recovery();

    cleanup();
    return bms::test::summary("test_journal_recovery");
}
