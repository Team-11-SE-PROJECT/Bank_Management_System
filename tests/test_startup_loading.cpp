#include <filesystem>
#include <fstream>
#include <string>

#include "infra/CustomerAccountStore.h"
#include "infra/AccountRepository.h"
#include "infra/TransactionRepository.h"
#include "TestHarness.h"

namespace fs = std::filesystem;
using namespace bms::infra;
using namespace bms::domain;

static const std::string kTestDir = "build-validate/test_startup_loading_data";

static void cleanup() {
    std::error_code ec;
    fs::remove_all(kTestDir, ec);
    fs::create_directories(kTestDir, ec);
}

static void writeFile(const std::string& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary);
    out << content;
}

static void test_missing_files() {
    cleanup();
    std::string custPath = kTestDir + "/nonexistent_cust.dat";
    std::string accPath = kTestDir + "/nonexistent_acc.dat";

    CustomerAccountStore store(custPath, accPath);
    auto status = store.loadWithStatus();
    CHECK(status.isOk());
    CHECK_EQ(store.customers().size(), 0U);
    CHECK_EQ(store.accounts().size(), 0U);

    AccountRepository accRepo(accPath);
    auto accStatus = accRepo.load();
    CHECK_EQ(accStatus, LoadStatus::MISSING);
    CHECK_EQ(accRepo.all().size(), 0U);

    TransactionRepository txnRepo(kTestDir + "/nonexistent_txn.dat");
    auto txnStatus = txnRepo.load();
    CHECK_EQ(txnStatus, LoadStatus::MISSING);
    CHECK_EQ(txnRepo.all().size(), 0U);
}

static void test_empty_files() {
    cleanup();
    std::string custPath = kTestDir + "/empty_cust.dat";
    std::string accPath = kTestDir + "/empty_acc.dat";
    writeFile(custPath, "");
    writeFile(accPath, "");

    CustomerAccountStore store(custPath, accPath);
    auto status = store.loadWithStatus();
    CHECK(status.isOk());
    CHECK_EQ(store.customers().size(), 0U);
    CHECK_EQ(store.accounts().size(), 0U);

    AccountRepository accRepo(accPath);
    auto accStatus = accRepo.load();
    CHECK(accStatus.isOk());
    CHECK_EQ(accRepo.all().size(), 0U);
}

static void test_empty_files_with_header_only() {
    cleanup();
    std::string custPath = kTestDir + "/cust_hdr.dat";
    std::string accPath = kTestDir + "/acc_hdr.dat";
    writeFile(custPath, "#VERSION 1\n");
    writeFile(accPath, "#VERSION 1\n");

    CustomerAccountStore store(custPath, accPath);
    auto status = store.loadWithStatus();
    CHECK(status.isOk());
    CHECK_EQ(store.customers().size(), 0U);
    CHECK_EQ(store.accounts().size(), 0U);
}

static void test_valid_loading() {
    cleanup();
    std::string custPath = kTestDir + "/customers.dat";
    std::string accPath = kTestDir + "/accounts.dat";

    writeFile(custPath,
        "#VERSION 1\n"
        "CUST1001|Alice Smith|9876543210|123 Park Ave\n"
        "CUST1002|Bob Jones|9876543211|456 Market St\n"
    );

    writeFile(accPath,
        "#VERSION 1\n"
        "1000000001|CUST1001|50000|ACTIVE|0|-\n"
        "1000000002|CUST1002|100000|ACTIVE|0|-\n"
    );

    CustomerAccountStore store(custPath, accPath);
    auto status = store.loadWithStatus();
    CHECK(status.isOk());
    CHECK_EQ(store.customers().size(), 2U);
    CHECK_EQ(store.accounts().size(), 2U);

    const auto* cust = store.findCustomer("CUST1001");
    CHECK(cust != nullptr);
    if (cust) {
        CHECK_EQ(cust->name, "Alice Smith");
        CHECK_EQ(cust->phone, "9876543210");
        CHECK_EQ(cust->address, "123 Park Ave");
    }

    const auto* acc = store.findAccount("1000000001");
    CHECK(acc != nullptr);
    if (acc) {
        CHECK_EQ(acc->customerId, "CUST1001");
        CHECK_EQ(acc->balancePaise, 50000LL);
        CHECK_EQ(acc->status, "ACTIVE");
    }
}

static void test_orphan_account_rejection() {
    cleanup();
    std::string custPath = kTestDir + "/customers.dat";
    std::string accPath = kTestDir + "/accounts.dat";

    writeFile(custPath,
        "#VERSION 1\n"
        "CUST1001|Alice Smith|9876543210|123 Park Ave\n"
    );

    writeFile(accPath,
        "#VERSION 1\n"
        "1000000001|CUST9999|50000|ACTIVE|0|-\n"
    );

    CustomerAccountStore store(custPath, accPath);
    auto status = store.loadWithStatus();
    CHECK(!status.isOk());
    CHECK_EQ(status.errorCode, LoadErrorCode::MALFORMED);
}

static void test_duplicate_customer_rejection() {
    cleanup();
    std::string custPath = kTestDir + "/customers.dat";
    std::string accPath = kTestDir + "/accounts.dat";

    writeFile(custPath,
        "#VERSION 1\n"
        "CUST1001|Alice Smith|9876543210|123 Park Ave\n"
        "CUST1001|Alice Duplicate|9876543210|123 Park Ave\n"
    );
    writeFile(accPath, "#VERSION 1\n");

    CustomerAccountStore store(custPath, accPath);
    auto status = store.loadWithStatus();
    CHECK(!status.isOk());
    CHECK_EQ(status.errorCode, LoadErrorCode::MALFORMED);
}

static void test_duplicate_account_rejection() {
    cleanup();
    std::string custPath = kTestDir + "/customers.dat";
    std::string accPath = kTestDir + "/accounts.dat";

    writeFile(custPath,
        "#VERSION 1\n"
        "CUST1001|Alice Smith|9876543210|123 Park Ave\n"
    );
    writeFile(accPath,
        "#VERSION 1\n"
        "1000000001|CUST1001|50000|ACTIVE|0|-\n"
        "1000000001|CUST1001|25000|ACTIVE|0|-\n"
    );

    CustomerAccountStore store(custPath, accPath);
    auto status = store.loadWithStatus();
    CHECK(!status.isOk());
    CHECK_EQ(status.errorCode, LoadErrorCode::MALFORMED);
}

int main() {
    test_missing_files();
    test_empty_files();
    test_empty_files_with_header_only();
    test_valid_loading();
    test_orphan_account_rejection();
    test_duplicate_customer_rejection();
    test_duplicate_account_rejection();

    cleanup();
    return bms::test::summary("test_startup_loading");
}
