#include <filesystem>
#include <fstream>
#include <string>

#include "infra/AccountRepository.h"
#include "infra/CustomerAccountStore.h"
#include "infra/Journal.h"
#include "infra/TransactionRepository.h"
#include "TestHarness.h"

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

int main() {
    test_malformed_accounts_quarantine();
    test_malformed_customers_quarantine();
    test_malformed_transactions_quarantine();
    test_unsupported_version_rejection();
    test_malformed_journal_quarantine_and_reset();

    cleanup();
    return bms::test::summary("test_file_error_handling");
}
