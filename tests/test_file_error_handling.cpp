#include <filesystem>
#include <fstream>
#include <string>

#include "infra/AccountRepository.h"
#include "infra/Config.h"
#include "infra/CustomerAccountStore.h"
#include "infra/Journal.h"
#include "infra/TransactionRepository.h"
#include "TestHarness.h"
#include "TestUtil.h"

namespace fs = std::filesystem;
using namespace bms::infra;

static const std::string kTestDir = "build-validate/test_file_error_handling_data";

static void cleanup() {
    std::error_code ec;
    fs::remove_all(kTestDir, ec);
    fs::create_directories(kTestDir, ec);
}

static void writeFile(const std::string& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary);
    out << content;
}

static void test_malformed_accounts_quarantine() {
    cleanup();
    std::string accPath = kTestDir + "/accounts.dat";
    std::string quarPath = accPath + ".corrupt.bak";

    writeFile(accPath,
        "#VERSION 1\n"
        "1000000001|CUST1001|SAVINGS|50000|ACTIVE\n"
        "CORRUPT_LINE_WITHOUT_DELIMITERS\n"
    );

    AccountRepository repo(accPath);
    auto status = repo.load();
    CHECK_EQ(status, LoadStatus::CORRUPT);
    CHECK_EQ(status.errorCode, LoadErrorCode::MALFORMED);

    // Verify original file was quarantined and quarantine file exists
    CHECK(fs::exists(quarPath));
}

static void test_malformed_customers_quarantine() {
    cleanup();
    std::string custPath = kTestDir + "/customers.dat";
    std::string accPath = kTestDir + "/accounts.dat";
    std::string quarPath = custPath + ".corrupt.bak";

    writeFile(custPath,
        "#VERSION 1\n"
        "CUST1001|Alice Smith\n" // Missing phone and address fields
    );
    writeFile(accPath, "#VERSION 1\n");

    CustomerAccountStore store(custPath, accPath);
    auto status = store.loadWithStatus();
    CHECK(!status.isOk());
    CHECK_EQ(status.errorCode, LoadErrorCode::MALFORMED);
    CHECK(fs::exists(quarPath));
}

static void test_malformed_transactions_quarantine() {
    cleanup();
    std::string txnPath = kTestDir + "/transactions.dat";
    std::string quarPath = txnPath + ".corrupt.bak";

    writeFile(txnPath,
        "#VERSION 1\n"
        "TXN001|1000000001|DEPOSIT|not_a_number|2026-03-30T10:00:00Z|Completed\n"
    );

    TransactionRepository repo(txnPath);
    auto status = repo.load();
    CHECK_EQ(status, LoadStatus::CORRUPT);
    CHECK_EQ(status.errorCode, LoadErrorCode::MALFORMED);
    CHECK(fs::exists(quarPath));
}

static void test_unsupported_version_rejection() {
    cleanup();
    std::string accPath = kTestDir + "/accounts.dat";

    writeFile(accPath,
        "#VERSION 99\n"
        "1000000001|CUST1001|SAVINGS|50000|ACTIVE\n"
    );

    AccountRepository repo(accPath);
    auto status = repo.load();
    CHECK_EQ(status, LoadStatus::CORRUPT);
    CHECK_EQ(status.errorCode, LoadErrorCode::UNSUPPORTED_VERSION);

    // Also test in CustomerAccountStore
    std::string custPath = kTestDir + "/customers.dat";
    writeFile(custPath,
        "#VERSION 99\n"
        "CUST1001|Alice Smith|9876543210|123 Park Ave\n"
    );
    CustomerAccountStore store(custPath, accPath);
    auto custStatus = store.loadWithStatus();
    CHECK_EQ(custStatus.errorCode, LoadErrorCode::UNSUPPORTED_VERSION);
}

static void test_malformed_journal_quarantine_and_reset() {
    cleanup();
    std::string jrnPath = kTestDir + "/journal.dat";
    std::string quarPath = jrnPath + ".corrupt.bak";

    writeFile(jrnPath,
        "#VERSION 1\n"
        "JRN001|TXN001|1000000001|DEPOSIT|100000|INVALID_STAGE\n" // Malformed stage
    );

    Journal journal(jrnPath);
    auto status = journal.load();
    CHECK_EQ(status, LoadStatus::CORRUPT);
    CHECK_EQ(status.errorCode, LoadErrorCode::MALFORMED);
    CHECK(fs::exists(quarPath));

    // Resetting should create clean valid journal
    CHECK(journal.clearAndReset());
    auto reloadStatus = journal.load();
    CHECK(reloadStatus.isOk());
    CHECK_EQ(journal.entries().size(), 0U);
}

static void test_invalid_config_values_report_errors_without_applying_partial_values() {
    cleanup();
    const std::string configPath = kTestDir + "/config.ini";
    Config config;

    writeFile(
        configPath,
        "daily_withdrawal_limit_paise=100000\n"
        "daily_withdrawal_limit_paise=-1\n"
    );
    const auto negativeStatus = config.load(configPath);
    CHECK_EQ(negativeStatus, LoadStatus::CORRUPT);
    CHECK(!config.lastError().empty());
    CHECK_EQ(config.dailyWithdrawalLimitPaise(), 2500000LL);

    writeFile(
        configPath,
        "daily_withdrawal_limit_paise=9223372036854775808\n"
    );
    const auto overflowStatus = config.load(configPath);
    CHECK_EQ(overflowStatus, LoadStatus::CORRUPT);
    CHECK(!config.lastError().empty());
    CHECK_EQ(config.dailyWithdrawalLimitPaise(), 2500000LL);
}

static void test_journal_missing_empty_and_unsupported_files() {
    cleanup();
    const std::string journalPath = kTestDir + "/journal.dat";

    Journal journal(journalPath);
    CHECK_EQ(journal.load().errorCode, LoadErrorCode::MISSING);

    writeFile(journalPath, "");
    CHECK(journal.load().isOk());
    CHECK(journal.entries().empty());

    writeFile(journalPath, "#VERSION invalid\n");
    CHECK_EQ(journal.load().errorCode, LoadErrorCode::MALFORMED);
    CHECK(fs::exists(journalPath + ".corrupt.bak"));

    writeFile(journalPath, "#VERSION 2\n");
    CHECK_EQ(journal.load().errorCode, LoadErrorCode::UNSUPPORTED_VERSION);
}

static void test_transaction_repository_header_and_truncated_record_paths() {
    cleanup();
    const std::string txnPath = kTestDir + "/transactions.dat";

    TransactionRepository transactions(txnPath);
    CHECK_EQ(transactions.load().errorCode, LoadErrorCode::MISSING);

    writeFile(txnPath, "");
    CHECK(transactions.load().isOk());
    CHECK(transactions.all().empty());

    writeFile(txnPath, "#VERSION invalid\n");
    CHECK_EQ(transactions.load().errorCode, LoadErrorCode::MALFORMED);
    CHECK(fs::exists(txnPath + ".corrupt.bak"));

    writeFile(txnPath, "#VERSION 2\n");
    CHECK_EQ(transactions.load().errorCode, LoadErrorCode::UNSUPPORTED_VERSION);

    const std::string validRecord =
        "TXN000007|DEPOSIT|BMS1001|CASH|100|100|"
        "2026-03-30 10:00:00|COMMITTED";
    writeFile(txnPath, "#VERSION 1\r\n\r\n" + validRecord + "\r\n");
    CHECK(transactions.load().isOk());
    CHECK_EQ(transactions.all().size(), 1u);
    CHECK_EQ(transactions.nextTxnId(), "TXN000008");

    writeFile(txnPath, "#VERSION 1\n" + validRecord.substr(0, 20));
    const auto truncated = transactions.load();
    CHECK_EQ(truncated.errorCode, LoadErrorCode::MALFORMED);
    CHECK(transactions.lastError().find("Truncated record")
          != std::string::npos);
}

static void test_transaction_repository_rejects_invalid_append_and_bad_ids() {
    cleanup();
    const std::string txnPath = kTestDir + "/transactions.dat";
    TransactionRepository transactions(txnPath);

    bms::domain::Transaction invalid;
    CHECK(!transactions.append(invalid));
    CHECK_EQ(transactions.nextTxnId(), "TXN000001");

    const std::string badIdRecord =
        "NOT_A_TXN|DEPOSIT|BMS1001|CASH|100|100|"
        "2026-03-30 10:00:00|COMMITTED\n";
    writeFile(txnPath, badIdRecord);
    CHECK(transactions.load().isOk());
    CHECK_EQ(transactions.nextTxnId(), "TXN000001");

    const std::string blockedParent = kTestDir + "/not-a-directory";
    writeFile(blockedParent, "file");
    TransactionRepository blocked(blockedParent + "/transactions.dat");
    CHECK(!blocked.append({
        "TXN000001", bms::domain::TxnType::DEPOSIT,
        "BMS1001", "CASH", 100, 100, "2026-03-30 10:00:00",
        bms::domain::TxnState::COMMITTED
    }));
}

static void test_account_repository_handles_missing_empty_and_duplicates() {
    cleanup();
    const std::string accountPath = kTestDir + "/accounts.dat";
    AccountRepository accounts(accountPath);

    CHECK_EQ(accounts.load().errorCode, LoadErrorCode::MISSING);

    writeFile(accountPath, "");
    CHECK(accounts.load().isOk());
    CHECK(accounts.find("BMS1001") == nullptr);
    CHECK(accounts.findByCustomer("CUST1001") == nullptr);

    writeFile(
        accountPath,
        "#VERSION 1\n"
        "BMS1001|CUST1001|100|ACTIVE|0|-\n"
        "BMS1001|CUST1002|200|ACTIVE|0|-\n"
    );
    CHECK_EQ(accounts.load().errorCode, LoadErrorCode::MALFORMED);
    CHECK(fs::exists(accountPath + ".corrupt.bak"));
}

static void test_customer_account_store_read_and_write_failures() {
    bms::test::TempDir dir;
    const std::string customersPath = dir.file("customers.dat");
    const std::string accountsPath = dir.file("accounts.dat");

    fs::create_directory(customersPath);
    CustomerAccountStore unreadableCustomers(customersPath, accountsPath);
    CHECK_EQ(
        unreadableCustomers.loadWithStatus().errorCode,
        LoadErrorCode::IO_ERROR
    );
    fs::remove(customersPath);

    const std::string unreadableAccountsPath = dir.file("blocked-accounts.dat");
    fs::create_directory(unreadableAccountsPath);
    CustomerAccountStore unreadableAccounts(
        dir.file("missing-customers.dat"),
        unreadableAccountsPath
    );
    CHECK_EQ(
        unreadableAccounts.loadWithStatus().errorCode,
        LoadErrorCode::IO_ERROR
    );

    CustomerAccountStore store(customersPath, accountsPath);
    CHECK(store.load());

    const bms::domain::Customer customer{
        "CUST1001", "Test Customer", "9876543210", "Bengaluru"
    };
    fs::create_directory(customersPath + ".tmp");
    CHECK(!store.addCustomer(customer));
    CHECK(store.customers().empty());
    fs::remove(customersPath + ".tmp");

    CHECK(store.addCustomer(customer));
    fs::create_directory(customersPath + ".tmp");
    auto updated = customer;
    updated.name = "Updated Customer";
    CHECK(!store.updateCustomer(updated));
    CHECK_EQ(store.customers()[0].name, customer.name);
    fs::remove(customersPath + ".tmp");

    bms::domain::Account account;
    account.accountNo = "BMS1001";
    account.customerId = customer.customerId;
    fs::create_directory(accountsPath + ".tmp");
    CHECK(!store.addAccount(account));
    CHECK(store.accounts().empty());
    fs::remove(accountsPath + ".tmp");
    CHECK(store.addAccount(account));
}

static void test_customer_account_store_headers_duplicates_and_updates() {
    bms::test::TempDir dir;
    const std::string customersPath = dir.file("customers.dat");
    const std::string accountsPath = dir.file("accounts.dat");

    writeFile(customersPath, "#VERSION invalid\n");
    CustomerAccountStore store(customersPath, accountsPath);
    CHECK_EQ(store.loadWithStatus().errorCode, LoadErrorCode::MALFORMED);
    CHECK(fs::exists(customersPath + ".corrupt.bak"));

    writeFile(customersPath, "#VERSION 2\n");
    CHECK_EQ(
        store.loadWithStatus().errorCode,
        LoadErrorCode::UNSUPPORTED_VERSION
    );

    const std::string customerLine =
        "CUST1001|Test Customer|9876543210|Bengaluru";
    writeFile(
        customersPath,
        "#VERSION 1\r\n\r\n" + customerLine + "\r\n"
        + customerLine + "\r\n"
    );
    const auto duplicateStatus = store.loadWithStatus();
    CHECK_EQ(duplicateStatus.errorCode, LoadErrorCode::MALFORMED);
    CHECK(duplicateStatus.errorMessage.find("Duplicate customer ID")
          != std::string::npos);

    writeFile(customersPath, "#VERSION 1\n" + customerLine + "\n");
    writeFile(accountsPath, "#VERSION invalid\n");
    CHECK_EQ(store.loadWithStatus().errorCode, LoadErrorCode::MALFORMED);
    CHECK(fs::exists(accountsPath + ".corrupt.bak"));

    writeFile(accountsPath, "#VERSION 2\n");
    CHECK_EQ(
        store.loadWithStatus().errorCode,
        LoadErrorCode::UNSUPPORTED_VERSION
    );

    const std::string accountLine = "BMS1001|CUST1001|100|ACTIVE|0|-";
    writeFile(accountsPath, "#VERSION 1\r\n\r\n" + accountLine + "\r\n");
    CHECK(store.load());
    CHECK(store.findAccount("BMS1001") != nullptr);
    CHECK(store.findAccount("MISSING") == nullptr);
    CHECK(store.findCustomer("MISSING") == nullptr);

    auto customer = *store.findCustomer("CUST1001");
    customer.name = "Updated Customer";
    CHECK(store.updateCustomer(customer));
    CHECK_EQ(store.findCustomer("CUST1001")->name, "Updated Customer");
    CHECK(!store.updateCustomer({
        "MISSING", "Name", "9876543210", "City"
    }));
    CHECK(!store.updateCustomer({
        "CUST1001", "", "9876543210", "City"
    }));
}

static void test_journal_repository_failure_and_identifier_paths() {
    bms::test::TempDir dir;
    const std::string missingPath = dir.file("missing-journal.dat");
    Journal journal(missingPath);

    bms::domain::JournalEntry invalid;
    CHECK(!journal.append(invalid));
    CHECK_EQ(journal.nextJournalId(), "JRN000001");

    const bms::domain::JournalEntry valid{
        "JRN000007", "TXN000001", "DEPOSIT", "-", "BMS1001", 100,
        bms::domain::JournalState::PENDING, "2026-03-30 10:00:00"
    };
    CHECK(journal.append(valid));
    CHECK_EQ(journal.nextJournalId(), "JRN000008");

    writeFile(
        missingPath,
        "#VERSION 1\n"
        "BAD-ID|TXN000001|DEPOSIT|-|BMS1001|100|PENDING|"
        "2026-03-30 10:00:00\n"
        "JRNnotnum|TXN000002|DEPOSIT|-|BMS1001|100|PENDING|"
        "2026-03-30 10:00:00\n"
    );
    CHECK(journal.load().isOk());
    CHECK_EQ(journal.nextJournalId(), "JRN000001");

    const std::string directoryPath = dir.file("journal-directory");
    fs::create_directory(directoryPath);
    Journal unreadable(directoryPath);
    CHECK_EQ(unreadable.load().errorCode, LoadErrorCode::IO_ERROR);
    CHECK(!unreadable.clearAndReset());
}

int main() {
    test_malformed_accounts_quarantine();
    test_malformed_customers_quarantine();
    test_malformed_transactions_quarantine();
    test_unsupported_version_rejection();
    test_malformed_journal_quarantine_and_reset();
    test_invalid_config_values_report_errors_without_applying_partial_values();
    test_journal_missing_empty_and_unsupported_files();
    test_transaction_repository_header_and_truncated_record_paths();
    test_transaction_repository_rejects_invalid_append_and_bad_ids();
    test_account_repository_handles_missing_empty_and_duplicates();
    test_customer_account_store_read_and_write_failures();
    test_customer_account_store_headers_duplicates_and_updates();
    test_journal_repository_failure_and_identifier_paths();

    cleanup();
    return bms::test::summary("test_file_error_handling");
}
