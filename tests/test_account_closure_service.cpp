#include "service/AccountClosureService.h"
#include "TestHarness.h"
#include "TestUtil.h"
#include "infra/AuditLogger.h"

#include <filesystem>
#include <string>

using namespace bms::domain;
using namespace bms::infra;
using namespace bms::service;
using bms::test::TempDir;

namespace {

struct Fixture {
    TempDir dir;
    AccountRepository accounts{dir.file("accounts.dat")};
    AuditLogger audit{dir.file("audit.log")};
    SessionManager admin;
    AccountClosureService service{accounts, &audit};

    explicit Fixture(long long balance = 0) {
        Account account;
        account.accountNo = "BMS1001";
        account.customerId = "CUST1001";
        account.balancePaise = balance;

        accounts.upsert(account);
        CHECK(accounts.save());

        admin.login("admin", Role::ADMIN, 300);
    }
};

void test_admin_closes_zero_balance_account() {
    Fixture f;

    CHECK(
        f.service.closeAccount(f.admin, "BMS1001")
        == Status::OK
    );

    const std::string auditText = bms::test::readFile(f.dir.file("audit.log"));
    CHECK(auditText.find("|ACCOUNT_CLOSED|admin|SUCCESS|accountNo=BMS1001") != std::string::npos);

    const auto* account = f.accounts.find("BMS1001");
    CHECK(account != nullptr);

    if (account != nullptr) {
        CHECK(account->status == "CLOSED");
        CHECK_EQ(account->balancePaise, 0);
    }
}

void test_closure_persists_after_reload() {
    Fixture f;

    CHECK(
        f.service.closeAccount(f.admin, "BMS1001")
        == Status::OK
    );

    AccountRepository reloaded(f.dir.file("accounts.dat"));
    CHECK(reloaded.load().isOk());

    const auto* account = reloaded.find("BMS1001");
    CHECK(account != nullptr);

    if (account != nullptr) {
        CHECK(account->status == "CLOSED");
        CHECK_EQ(account->balancePaise, 0);
    }
}

void test_nonzero_balance_account_cannot_be_closed() {
    Fixture f(50000);

    CHECK(
        f.service.closeAccount(f.admin, "BMS1001")
        == Status::INVALID_INPUT
    );

    CHECK(f.accounts.find("BMS1001")->status == "ACTIVE");
    CHECK_EQ(f.accounts.find("BMS1001")->balancePaise, 50000);
}

void test_already_closed_account_cannot_be_closed_again() {
    Fixture f;

    f.accounts.find("BMS1001")->status = "CLOSED";
    CHECK(f.accounts.save());

    CHECK(
        f.service.closeAccount(f.admin, "BMS1001")
        == Status::INVALID_INPUT
    );

    CHECK(f.accounts.find("BMS1001")->status == "CLOSED");
}

void test_missing_and_empty_account_numbers() {
    Fixture f;

    CHECK(
        f.service.closeAccount(f.admin, "")
        == Status::INVALID_INPUT
    );

    CHECK(
        f.service.closeAccount(f.admin, "UNKNOWN")
        == Status::NOT_FOUND
    );

    CHECK(f.accounts.find("BMS1001")->status == "ACTIVE");
}

void test_customer_and_inactive_sessions_are_rejected() {
    Fixture f;

    SessionManager customer;
    customer.login("customer", Role::CUSTOMER, 300);

    CHECK(
        f.service.closeAccount(customer, "BMS1001")
        == Status::UNAUTHORIZED
    );

    SessionManager inactive;

    CHECK(
        f.service.closeAccount(inactive, "BMS1001")
        == Status::UNAUTHORIZED
    );

    CHECK(f.accounts.find("BMS1001")->status == "ACTIVE");
}

void test_storage_failure_restores_original_account() {
    Fixture f;

    // Force atomicWrite to fail.
    std::filesystem::create_directory(
        f.dir.file("accounts.dat.tmp")
    );

    CHECK(
        f.service.closeAccount(f.admin, "BMS1001")
        == Status::STORAGE_FAILURE
    );

    CHECK(f.accounts.find("BMS1001")->status == "ACTIVE");

    AccountRepository reloaded(f.dir.file("accounts.dat"));
    CHECK(reloaded.load().isOk());

    const auto* account = reloaded.find("BMS1001");
    CHECK(account != nullptr);

    if (account != nullptr) {
        CHECK(account->status == "ACTIVE");
    }
}

} // namespace

int main() {
    test_admin_closes_zero_balance_account();
    test_closure_persists_after_reload();
    test_nonzero_balance_account_cannot_be_closed();
    test_already_closed_account_cannot_be_closed_again();
    test_missing_and_empty_account_numbers();
    test_customer_and_inactive_sessions_are_rejected();
    test_storage_failure_restores_original_account();

    return bms::test::summary("test_account_closure_service");
}
