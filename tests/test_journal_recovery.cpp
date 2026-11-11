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

    // Initial account on disk has balance 100,000 paise (Rs. 1000.00)
    {
        AccountRepository initialAccounts(accPath);
        Account acc;
        acc.accountNo = "1000000001";
        acc.customerId = "CUST1001";
        acc.balancePaise = 100000;
        acc.status = "ACTIVE";
        initialAccounts.upsert(acc);
        CHECK(initialAccounts.save());
    }

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

    AccountRepository accRepo(accPath);
    TransactionRepository txnRepo(txnPath);
    Journal journal(jrnPath);
    AuditLogger audit(auditPath);

    // Load fresh repositories to simulate startup after the crash.
    CHECK(accRepo.load().isOk());
    CHECK_EQ(txnRepo.load(), LoadStatus::MISSING);
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

    // Transaction should now be recorded in the reloaded repository.
    CHECK_EQ(txnRepo.all().size(), 1U);
    const auto& txns = txnRepo.all();
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

    // Recovery from a second fresh startup must not apply the deposit twice.
    AccountRepository restartedAccounts(accPath);
    TransactionRepository restartedTransactions(txnPath);
    Journal restartedJournal(jrnPath);
    CHECK(restartedAccounts.load().isOk());
    CHECK(restartedTransactions.load().isOk());
    CHECK_EQ(Journal::recover(
        restartedJournal, restartedAccounts, restartedTransactions, &audit
    ), 0);
    const auto* balanceAfterRestart =
        restartedAccounts.find("1000000001");
    CHECK(balanceAfterRestart != nullptr);
    if (balanceAfterRestart != nullptr) {
        CHECK_EQ(balanceAfterRestart->balancePaise, 150000LL);
    }
    CHECK_EQ(restartedTransactions.all().size(), 1U);
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

static void test_recovery_reconciles_pending_and_applied_entries() {
    cleanup();
    const std::string accPath = kTestDir + "/accounts.dat";
    const std::string txnPath = kTestDir + "/transactions.dat";
    const std::string jrnPath = kTestDir + "/journal.dat";

    AccountRepository accounts(accPath);
    Account first;
    first.accountNo = "1000000001";
    first.customerId = "CUST1001";
    first.balancePaise = 70000;
    accounts.upsert(first);

    Account second;
    second.accountNo = "1000000002";
    second.customerId = "CUST1002";
    second.balancePaise = 100000;
    accounts.upsert(second);
    CHECK(accounts.save());

    TransactionRepository transactions(txnPath);
    CHECK(transactions.append({
        "TXN00000001", TxnType::DEPOSIT, "1000000001", "CASH",
        10000, 70000, "2026-03-30 10:00:00", TxnState::COMMITTED
    }));
    CHECK(transactions.append({
        "TXN00000002", TxnType::DEPOSIT, "1000000001", "CASH",
        5000, 65000, "2026-03-30 10:01:00", TxnState::PENDING
    }));

    const std::vector<JournalEntry> entries = {
        {
            "JRN00000001", "TXN00000001", "DEPOSIT", "-", "1000000001",
            10000, JournalState::PENDING, "2026-03-30 10:00:00"
        },
        {
            "JRN00000002", "TXN00000002", "DEPOSIT", "-", "1000000001",
            5000, JournalState::APPLIED, "2026-03-30 10:01:00"
        },
        {
            "JRN00000003", "TXN00000003", "WITHDRAWAL", "-",
            "1000000002", 20000, JournalState::APPLIED,
            "2026-03-30 10:02:00"
        },
        {
            "JRN00000004", "TXN00000004", "DEPOSIT", "-", "MISSING",
            1000, JournalState::APPLIED, "2026-03-30 10:03:00"
        },
        {
            "JRN00000005", "TXN00000005", "DEPOSIT", "-", "1000000001",
            1000, JournalState::COMMITTED, "2026-03-30 10:04:00"
        },
        {
            "JRN00000006", "TXN00000006", "DEPOSIT", "-", "1000000001",
            1000, JournalState::ABORTED, "2026-03-30 10:05:00"
        }
    };

    std::string journalContents = "#VERSION 1\n";
    for (const JournalEntry& entry : entries) {
        std::string line;
        std::string error;
        CHECK(format::serializeJournalEntry(entry, line, error));
        journalContents += line + "\n";
    }
    writeFile(jrnPath, journalContents);

    Journal journal(jrnPath);
    CHECK_EQ(Journal::recover(journal, accounts, transactions), 4);

    const Account* reconciledFirst = accounts.find("1000000001");
    const Account* reconciledSecond = accounts.find("1000000002");
    CHECK(reconciledFirst != nullptr);
    CHECK(reconciledSecond != nullptr);
    if (reconciledFirst) {
        CHECK_EQ(reconciledFirst->balancePaise, 65000);
    }
    if (reconciledSecond) {
        CHECK_EQ(reconciledSecond->balancePaise, 80000);
    }

    CHECK_EQ(transactions.all().size(), 4u);
    CHECK(transactions.all()[2].state == TxnState::COMMITTED);
    CHECK_EQ(transactions.all()[2].txnId, "TXN00000002");
    CHECK(transactions.all()[3].type == TxnType::WITHDRAWAL);
    CHECK_EQ(transactions.all()[3].balanceAfterPaise, 80000);
    CHECK_EQ(journal.load(), LoadStatus::OK);
    CHECK_EQ(journal.entries().size(), 10u);
    CHECK(journal.entries()[6].state == JournalState::COMMITTED);
    CHECK(journal.entries()[7].state == JournalState::COMMITTED);
    CHECK(journal.entries()[8].state == JournalState::COMMITTED);
    CHECK(journal.entries()[9].state == JournalState::ABORTED);
}

static void test_recovery_handles_empty_missing_and_corrupt_journals() {
    cleanup();
    const std::string accPath = kTestDir + "/accounts.dat";
    const std::string txnPath = kTestDir + "/transactions.dat";
    const std::string jrnPath = kTestDir + "/journal.dat";
    AccountRepository accounts(accPath);
    TransactionRepository transactions(txnPath);

    Journal missing(jrnPath);
    CHECK_EQ(Journal::recover(missing, accounts, transactions, nullptr), 0);

    writeFile(jrnPath, "");
    Journal empty(jrnPath);
    CHECK_EQ(Journal::recover(empty, accounts, transactions, nullptr), 0);

    writeFile(jrnPath, "#VERSION invalid\n");
    Journal corrupt(jrnPath);
    CHECK_EQ(Journal::recover(corrupt, accounts, transactions, nullptr), 0);
    CHECK(corrupt.load().isOk());
    CHECK(corrupt.entries().empty());
}

static void test_recovery_applied_entry_with_existing_commit() {
    cleanup();
    const std::string accPath = kTestDir + "/accounts.dat";
    const std::string txnPath = kTestDir + "/transactions.dat";
    const std::string jrnPath = kTestDir + "/journal.dat";

    AccountRepository accounts(accPath);
    Account account;
    account.accountNo = "1000000001";
    account.customerId = "CUST1001";
    account.balancePaise = 90000;
    accounts.upsert(account);
    CHECK(accounts.save());

    TransactionRepository transactions(txnPath);
    CHECK(transactions.append({
        "TXN00000001", TxnType::DEPOSIT, "1000000001", "CASH",
        10000, 90000, "2026-03-30 10:00:00", TxnState::COMMITTED
    }));

    JournalEntry applied{
        "BAD-ID", "TXN00000001", "DEPOSIT", "-", "1000000001",
        10000, JournalState::APPLIED, "2026-03-30 10:00:00"
    };
    std::string line;
    std::string error;
    CHECK(format::serializeJournalEntry(applied, line, error));
    writeFile(jrnPath, "#VERSION 1\n" + line + "\n");

    Journal journal(jrnPath);
    CHECK_EQ(Journal::recover(journal, accounts, transactions, nullptr), 1);
    CHECK_EQ(journal.entries().size(), 2u);
    CHECK_EQ(journal.entries().back().journalId, "JRN000001");
    CHECK(journal.entries().back().state == JournalState::COMMITTED);
    CHECK_EQ(transactions.all().size(), 1u);
    CHECK_EQ(accounts.find("1000000001")->balancePaise, 90000);
}

int main() {
    test_pending_crash_recovery();
    test_applied_crash_recovery_deposit();
    test_idempotent_recovery();
    test_recovery_reconciles_pending_and_applied_entries();
    test_recovery_handles_empty_missing_and_corrupt_journals();
    test_recovery_applied_entry_with_existing_commit();

    cleanup();
    return bms::test::summary("test_journal_recovery");
}
