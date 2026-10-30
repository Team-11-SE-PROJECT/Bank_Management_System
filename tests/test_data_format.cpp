#include "infra/DataFormat.h"
#include "TestHarness.h"

using namespace bms::infra::format;
using namespace bms::domain;

static void test_version_headers() {
    CHECK(isVersionHeader("#VERSION 1"));
    CHECK(isVersionHeader("#VERSION 20"));
    CHECK(!isVersionHeader("VERSION 1"));
    CHECK(!isVersionHeader("#VER 1"));
    CHECK(!isVersionHeader(""));

    int ver = 0;
    std::string err;
    CHECK(parseVersionHeader("#VERSION 1", ver, err));
    CHECK_EQ(ver, 1);

    CHECK(parseVersionHeader("#VERSION 42", ver, err));
    CHECK_EQ(ver, 42);

    CHECK(!parseVersionHeader("#VERSION 0", ver, err));
    CHECK(!parseVersionHeader("#VERSION -1", ver, err));
    CHECK(!parseVersionHeader("#VERSION abc", ver, err));
    CHECK(!parseVersionHeader("#VERSION", ver, err));

    CHECK_EQ(makeVersionHeader(1), "#VERSION 1");
    CHECK_EQ(makeVersionHeader(5), "#VERSION 5");
}

static void test_customer_format() {
    Customer c1{"CUST0001", "Alice Smith", "9876543210", "123 Main St, Bangalore"};
    std::string line;
    std::string err;

    CHECK(serializeCustomer(c1, line, err));
    CHECK_EQ(line, "CUST0001|Alice Smith|9876543210|123 Main St, Bangalore");

    Customer cOut;
    auto status = deserializeCustomer(line, cOut, 1);
    CHECK(status.isOk());
    CHECK_EQ(cOut.customerId, "CUST0001");
    CHECK_EQ(cOut.name, "Alice Smith");
    CHECK_EQ(cOut.phone, "9876543210");
    CHECK_EQ(cOut.address, "123 Main St, Bangalore");

    // Rejection of embedded pipes or newlines
    Customer badPipe{"CUST0001", "Alice|Smith", "9876543210", "Bangalore"};
    CHECK(!serializeCustomer(badPipe, line, err));

    Customer badNewline{"CUST0001", "Alice\nSmith", "9876543210", "Bangalore"};
    CHECK(!serializeCustomer(badNewline, line, err));

    // Invalid phone (not 10 digits)
    Customer badPhone{"CUST0001", "Alice Smith", "12345", "Bangalore"};
    CHECK(!serializeCustomer(badPhone, line, err));

    // Deserialization with wrong field count
    CHECK(!deserializeCustomer("CUST0001|Alice|9876543210", cOut, 2).isOk());
    CHECK(!deserializeCustomer("CUST0001|Alice|9876543210|Address|Extra", cOut, 3).isOk());

    // Empty required fields
    CHECK(!deserializeCustomer("|Alice|9876543210|Address", cOut, 4).isOk());
    CHECK(!deserializeCustomer("CUST0001||9876543210|Address", cOut, 5).isOk());
    CHECK(!deserializeCustomer("CUST0001|Alice|123|Address", cOut, 6).isOk());
}

static void test_account_format() {
    Account a1{"BMS000001", "CUST0001", 150000, "ACTIVE", 25000, "2026-10-30"};
    std::string line;
    std::string err;

    CHECK(serializeAccount(a1, line, err));
    CHECK_EQ(line, "BMS000001|CUST0001|150000|ACTIVE|25000|2026-10-30");

    Account aOut;
    auto status = deserializeAccount(line, aOut, 1);
    CHECK(status.isOk());
    CHECK_EQ(aOut.accountNo, "BMS000001");
    CHECK_EQ(aOut.customerId, "CUST0001");
    CHECK_EQ(aOut.balancePaise, 150000);
    CHECK_EQ(aOut.status, "ACTIVE");
    CHECK_EQ(aOut.withdrawnTodayPaise, 25000);
    CHECK_EQ(aOut.lastWithdrawDate, "2026-10-30");

    // Empty date serializes as "-"
    Account a2{"BMS000002", "CUST0002", 0, "CLOSED", 0, ""};
    CHECK(serializeAccount(a2, line, err));
    CHECK_EQ(line, "BMS000002|CUST0002|0|CLOSED|0|-");

    CHECK(deserializeAccount(line, aOut, 2).isOk());
    CHECK_EQ(aOut.lastWithdrawDate, "");
    CHECK_EQ(aOut.status, "CLOSED");

    // Rejection of negative balance
    Account badBal = a1;
    badBal.balancePaise = -50;
    CHECK(!serializeAccount(badBal, line, err));

    // Rejection of invalid status
    Account badStatus = a1;
    badStatus.status = "SUSPENDED";
    CHECK(!serializeAccount(badStatus, line, err));

    // Deserialization validation
    CHECK(!deserializeAccount("BMS001|CUST1|-100|ACTIVE|0|-", aOut, 3).isOk());
    CHECK(!deserializeAccount("BMS001|CUST1|100|UNKNOWN|0|-", aOut, 4).isOk());
    CHECK(!deserializeAccount("BMS001|CUST1|100|ACTIVE|0|2026-13-40", aOut, 5).isOk());
}

static void test_transaction_format() {
    Transaction t1{"TXN000001", TxnType::DEPOSIT, "BMS000001", "CASH", 50000, 150000, "2026-10-30 14:30:00", TxnState::COMMITTED};
    std::string line;
    std::string err;

    CHECK(serializeTransaction(t1, line, err));
    CHECK_EQ(line, "TXN000001|DEPOSIT|BMS000001|CASH|50000|150000|2026-10-30 14:30:00|COMMITTED");

    Transaction tOut;
    auto status = deserializeTransaction(line, tOut, 1);
    CHECK(status.isOk());
    CHECK_EQ(tOut.txnId, "TXN000001");
    CHECK(tOut.type == TxnType::DEPOSIT);
    CHECK_EQ(tOut.accountNo, "BMS000001");
    CHECK_EQ(tOut.counterparty, "CASH");
    CHECK_EQ(tOut.amountPaise, 50000);
    CHECK_EQ(tOut.balanceAfterPaise, 150000);
    CHECK_EQ(tOut.timestamp, "2026-10-30 14:30:00");
    CHECK(tOut.state == TxnState::COMMITTED);

    // Negative / zero amount rejection
    Transaction badAmt = t1;
    badAmt.amountPaise = 0;
    CHECK(!serializeTransaction(badAmt, line, err));

    badAmt.amountPaise = -100;
    CHECK(!serializeTransaction(badAmt, line, err));

    // Bad timestamp
    Transaction badTs = t1;
    badTs.timestamp = "invalid timestamp";
    CHECK(!serializeTransaction(badTs, line, err));

    // Deserialization invalid state / type
    CHECK(!deserializeTransaction("TXN1|TRANSFER|BMS1|-|100|200|2026-10-30 12:00:00|COMMITTED", tOut, 2).isOk());
    CHECK(!deserializeTransaction("TXN1|DEPOSIT|BMS1|-|100|200|2026-10-30 12:00:00|UNKNOWN", tOut, 3).isOk());
    CHECK(!deserializeTransaction("TXN1|DEPOSIT|BMS1|-|0|200|2026-10-30 12:00:00|COMMITTED", tOut, 4).isOk());
}

static void test_journal_format() {
    JournalEntry j1{"JRN000001", "TXN000001", "DEPOSIT", "-", "BMS000001", 50000, JournalState::APPLIED, "2026-10-30 14:30:00"};
    std::string line;
    std::string err;

    CHECK(serializeJournalEntry(j1, line, err));
    CHECK_EQ(line, "JRN000001|TXN000001|DEPOSIT|-|BMS000001|50000|APPLIED|2026-10-30 14:30:00");

    JournalEntry jOut;
    auto status = deserializeJournalEntry(line, jOut, 1);
    CHECK(status.isOk());
    CHECK_EQ(jOut.journalId, "JRN000001");
    CHECK_EQ(jOut.transactionId, "TXN000001");
    CHECK_EQ(jOut.type, "DEPOSIT");
    CHECK_EQ(jOut.fromAccountId, "-");
    CHECK_EQ(jOut.toAccountId, "BMS000001");
    CHECK_EQ(jOut.amountPaise, 50000);
    CHECK(jOut.state == JournalState::APPLIED);
    CHECK_EQ(jOut.timestamp, "2026-10-30 14:30:00");

    // Deserialization checks
    CHECK(!deserializeJournalEntry("JRN1|TXN1|INVALID|-|BMS1|5000|PENDING|2026-10-30 14:30:00", jOut, 2).isOk());
    CHECK(!deserializeJournalEntry("JRN1|TXN1|DEPOSIT|-|BMS1|-5000|PENDING|2026-10-30 14:30:00", jOut, 3).isOk());
    CHECK(!deserializeJournalEntry("JRN1|TXN1|DEPOSIT|-|BMS1|5000|INVALID_STATE|2026-10-30 14:30:00", jOut, 4).isOk());
    CHECK(!deserializeJournalEntry("JRN1|TXN1|DEPOSIT|-|BMS1|5000|PENDING|bad-time", jOut, 5).isOk());
}

int main() {
    test_version_headers();
    test_customer_format();
    test_account_format();
    test_transaction_format();
    test_journal_format();

    return bms::test::summary("test_data_format");
}
