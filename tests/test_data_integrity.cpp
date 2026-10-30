#include "infra/DataIntegrityValidator.h"
#include "TestHarness.h"

using namespace bms::infra;
using namespace bms::domain;

static void test_customers_integrity() {
    // Valid file
    const std::string valid =
        "#VERSION 1\n"
        "CUST0001|John Doe|9876543210|Bangalore\n"
        "CUST0002|Jane Smith|9123456789|Mumbai\n";

    auto res = DataIntegrityValidator::validateCustomers(valid);
    CHECK(res.valid);
    CHECK_EQ(res.errors.size(), 0u);

    // Unsupported version
    const std::string badVersion =
        "#VERSION 99\n"
        "CUST0001|John Doe|9876543210|Bangalore\n";
    res = DataIntegrityValidator::validateCustomers(badVersion);
    CHECK(!res.valid);
    CHECK(res.errors[0].field == "version");

    // Duplicate customer ID
    const std::string duplicates =
        "CUST0001|John Doe|9876543210|Bangalore\n"
        "CUST0001|Another John|9876543211|Delhi\n";
    res = DataIntegrityValidator::validateCustomers(duplicates);
    CHECK(!res.valid);
    CHECK_EQ(res.errors[0].field, "customerId");

    // Invalid phone
    const std::string badPhone =
        "CUST0001|John Doe|12345|Bangalore\n";
    res = DataIntegrityValidator::validateCustomers(badPhone);
    CHECK(!res.valid);
}

static void test_accounts_integrity() {
    // Valid
    const std::string valid =
        "#VERSION 1\n"
        "BMS000001|CUST0001|100000|ACTIVE|0|2026-10-30\n"
        "BMS000002|CUST0002|0|CLOSED|0|-\n";
    auto res = DataIntegrityValidator::validateAccounts(valid);
    CHECK(res.valid);

    // Negative account number or negative signs
    const std::string badAccNo =
        "BMS-00001|CUST0001|100000|ACTIVE|0|-\n";
    res = DataIntegrityValidator::validateAccounts(badAccNo);
    CHECK(!res.valid);

    // Negative balance
    const std::string negBal =
        "BMS000001|CUST0001|-500|ACTIVE|0|-\n";
    res = DataIntegrityValidator::validateAccounts(negBal);
    CHECK(!res.valid);

    // Duplicate account number
    const std::string dupAcc =
        "BMS000001|CUST0001|100000|ACTIVE|0|-\n"
        "BMS000001|CUST0002|200000|ACTIVE|0|-\n";
    res = DataIntegrityValidator::validateAccounts(dupAcc);
    CHECK(!res.valid);
    CHECK_EQ(res.errors[0].field, "accountNo");
}

static void test_transactions_integrity() {
    const std::string valid =
        "#VERSION 1\n"
        "TXN000001|DEPOSIT|BMS000001|CASH|50000|150000|2026-10-30 10:00:00|COMMITTED\n";
    auto res = DataIntegrityValidator::validateTransactions(valid);
    CHECK(res.valid);

    // Zero / negative amount
    const std::string zeroAmt =
        "TXN000001|DEPOSIT|BMS000001|CASH|0|150000|2026-10-30 10:00:00|COMMITTED\n";
    res = DataIntegrityValidator::validateTransactions(zeroAmt);
    CHECK(!res.valid);

    // Bad timestamp
    const std::string badTs =
        "TXN000001|DEPOSIT|BMS000001|CASH|5000|150000|not-a-timestamp|COMMITTED\n";
    res = DataIntegrityValidator::validateTransactions(badTs);
    CHECK(!res.valid);
}

static void test_cross_references() {
    Customer c1{"CUST0001", "Alice", "9876543210", "City"};
    Customer c2{"CUST0002", "Bob", "9876543211", "City"};

    Account a1;
    a1.accountNo = "BMS000001";
    a1.customerId = "CUST0001";

    Account aOrphan;
    aOrphan.accountNo = "BMS000002";
    aOrphan.customerId = "CUST9999"; // Non-existent

    // Valid cross reference
    auto res1 = DataIntegrityValidator::validateCrossReferences({a1}, {c1, c2});
    CHECK(res1.valid);

    // Orphan account
    auto res2 = DataIntegrityValidator::validateCrossReferences({a1, aOrphan}, {c1, c2});
    CHECK(!res2.valid);
    CHECK_EQ(res2.errors[0].field, "customerId");

    // Transaction references
    Transaction t1{"TXN001", TxnType::DEPOSIT, "BMS000001", "CASH", 100, 100, "2026-10-30 10:00:00", TxnState::COMMITTED};
    Transaction tOrphan{"TXN002", TxnType::DEPOSIT, "BMS999999", "CASH", 100, 100, "2026-10-30 10:00:00", TxnState::COMMITTED};

    auto resTxn1 = DataIntegrityValidator::validateTransactionReferences({t1}, {a1});
    CHECK(resTxn1.valid);

    auto resTxn2 = DataIntegrityValidator::validateTransactionReferences({t1, tOrphan}, {a1});
    CHECK(!resTxn2.valid);
    CHECK_EQ(resTxn2.errors[0].field, "accountNo");
}

int main() {
    test_customers_integrity();
    test_accounts_integrity();
    test_transactions_integrity();
    test_cross_references();

    return bms::test::summary("test_data_integrity");
}
