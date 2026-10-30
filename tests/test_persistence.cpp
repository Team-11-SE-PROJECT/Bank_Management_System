#include <filesystem>
#include <fstream>
#include <string>

#include "domain/Account.h"
#include "domain/Customer.h"
#include "domain/Transaction.h"
#include "infra/AccountRepository.h"
#include "infra/AuditLogger.h"
#include "infra/Config.h"
#include "infra/CredentialStore.h"
#include "infra/CustomerAccountStore.h"
#include "infra/TransactionRepository.h"
#include "service/AccountService.h"
#include "service/TransactionService.h"
#include "TestHarness.h"

namespace fs = std::filesystem;
using namespace bms::infra;
using namespace bms::domain;
using namespace bms::service;

static const std::string kTestDir = "build-validate/test_persistence_data";

static void cleanup() {
    std::error_code ec;
    fs::remove_all(kTestDir, ec);
    fs::create_directories(kTestDir, ec);
}

static void test_account_creation_persistence() {
    cleanup();
    std::string custPath = kTestDir + "/customers.dat";
    std::string accPath = kTestDir + "/accounts.dat";
    std::string credPath = kTestDir + "/credentials.dat";

    CredentialStore credStore(credPath);
    CustomerAccountStore store(custPath, accPath);
    CHECK(store.load());

    AccountService accountService(store, credStore);

    CustomerInfo info1;
    info1.name = "Alice Wonder";
    info1.phone = "9876543210";
    info1.address = "12 Wonderland Ave";
    info1.initialPin = "1234";

    std::string accNo1;
    auto res1 = accountService.createAccount(info1, accNo1);
    CHECK_EQ(res1, bms::domain::Status::OK);
    CHECK(!accNo1.empty());

    CustomerInfo info2;
    info2.name = "Bob Builder";
    info2.phone = "9876543211";
    info2.address = "34 Brick Rd";
    info2.initialPin = "5678";

    std::string accNo2;
    auto res2 = accountService.createAccount(info2, accNo2);
    CHECK_EQ(res2, bms::domain::Status::OK);
    CHECK(!accNo2.empty());
    CHECK(accNo1 != accNo2);

    // Simulate system restart: instantiate brand new store and repositories
    CustomerAccountStore reloadedStore(custPath, accPath);
    CHECK(reloadedStore.load());
    CHECK_EQ(reloadedStore.customers().size(), 2U);
    CHECK_EQ(reloadedStore.accounts().size(), 2U);

    const auto* a1 = reloadedStore.findAccount(accNo1);
    CHECK(a1 != nullptr);
    if (a1) {
        CHECK_EQ(a1->balancePaise, 0LL);
        CHECK_EQ(a1->status, "ACTIVE");
        const auto* c1 = reloadedStore.findCustomer(a1->customerId);
        CHECK(c1 != nullptr);
        if (c1) {
            CHECK_EQ(c1->name, "Alice Wonder");
            CHECK_EQ(c1->phone, "9876543210");
        }
    }

    const auto* a2 = reloadedStore.findAccount(accNo2);
    CHECK(a2 != nullptr);
    if (a2) {
        CHECK_EQ(a2->balancePaise, 0LL);
        CHECK_EQ(a2->status, "ACTIVE");
        const auto* c2 = reloadedStore.findCustomer(a2->customerId);
        CHECK(c2 != nullptr);
        if (c2) {
            CHECK_EQ(c2->name, "Bob Builder");
        }
    }
}

static void test_transaction_persistence() {
    cleanup();
    std::string accPath = kTestDir + "/accounts.dat";
    std::string txnPath = kTestDir + "/transactions.dat";
    std::string auditPath = kTestDir + "/audit.log";

    AccountRepository accRepo(accPath);
    TransactionRepository txnRepo(txnPath);

    // Setup an initial account
    Account initialAcc;
    initialAcc.accountNo = "1000000001";
    initialAcc.customerId = "CUST1001";
    initialAcc.balancePaise = 500000; // Rs. 5000.00
    initialAcc.status = "ACTIVE";
    accRepo.upsert(initialAcc);
    accRepo.save();

    Config cfg;
    AuditLogger audit(auditPath);
    TransactionService txnService(accRepo, txnRepo, cfg, audit);

    // Deposit 2000.00 (200000 paise)
    auto depRes = txnService.deposit("1000000001", 200000, DepositMode::CASH);
    CHECK_EQ(depRes.status, TxnStatus::OK);
    CHECK_EQ(depRes.newBalancePaise, 700000LL);

    // Withdraw 1500.00 (150000 paise)
    auto withRes = txnService.withdraw("1000000001", 150000);
    CHECK_EQ(withRes.status, TxnStatus::OK);
    CHECK_EQ(withRes.newBalancePaise, 550000LL);

    // Simulate system restart: reload fresh repositories from disk
    AccountRepository reloadedAccRepo(accPath);
    CHECK(reloadedAccRepo.load().isOk());
    const auto* acc = reloadedAccRepo.find("1000000001");
    CHECK(acc != nullptr);
    if (acc) {
        CHECK_EQ(acc->balancePaise, 550000LL);
    }

    TransactionRepository reloadedTxnRepo(txnPath);
    CHECK(reloadedTxnRepo.load().isOk());
    const auto& txns = reloadedTxnRepo.all();
    // In BMS 2-phase txn logging: each transaction logs PENDING then COMMITTED
    CHECK_EQ(txns.size(), 4U);
    if (txns.size() >= 4U) {
        // First txn: deposit
        CHECK_EQ(txns[1].accountNo, "1000000001");
        CHECK_EQ(txns[1].amountPaise, 200000LL);
        CHECK(txns[1].type == TxnType::DEPOSIT);
        CHECK(txns[1].state == TxnState::COMMITTED);

        // Second txn: withdrawal
        CHECK_EQ(txns[3].accountNo, "1000000001");
        CHECK_EQ(txns[3].amountPaise, 150000LL);
        CHECK(txns[3].type == TxnType::WITHDRAWAL);
        CHECK(txns[3].state == TxnState::COMMITTED);
    }
}

int main() {
    test_account_creation_persistence();
    test_transaction_persistence();

    cleanup();
    return bms::test::summary("test_persistence");
}
