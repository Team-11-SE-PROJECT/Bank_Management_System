#include "TestHarness.h"
#include "TestUtil.h"

#include "domain/Account.h"
#include "domain/Customer.h"
#include "domain/Status.h"
#include "infra/CredentialStore.h"
#include "infra/CustomerAccountStore.h"
#include "service/AccountService.h"

#include <array>
#include <filesystem>
#include <string>

namespace {

using bms::domain::Account;
using bms::domain::Customer;
using bms::domain::Status;
using bms::infra::Credential;
using bms::infra::CredentialStore;
using bms::infra::CustomerAccountStore;
using bms::service::AccountService;
using bms::service::CustomerInfo;

struct Fixture {
    bms::test::TempDir dir;
    CustomerAccountStore store{
        dir.file("customers.dat"), dir.file("accounts.dat")
    };
    CredentialStore credentials{dir.file("credentials.dat")};

    AccountService service{store, credentials};
};

CustomerInfo validInfo() {
    return {"Test Customer", "9876543210", "Bengaluru", "1234"};
}

void test_input_validation_edges() {
    Fixture fixture;
    const std::array<CustomerInfo, 9> invalidInputs = {{
        {"", "9876543210", "Bengaluru", "1234"},
        {"Name", "", "Bengaluru", "1234"},
        {"Name", "9876543210", "", "1234"},
        {"Name|Other", "9876543210", "Bengaluru", "1234"},
        {"Name", "9876543210", "Bengaluru\n", "1234"},
        {"Name", "987654321x", "Bengaluru", "1234"},
        {"Name", "9876543210", "Bengaluru", "123"},
        {"Name", "9876543210", "Bengaluru", "1234567"},
        {"Name", "9876543210", "Bengaluru", "12A4"}
    }};

    for (const CustomerInfo& input : invalidInputs) {
        std::string accountNo = "stale";
        CHECK_EQ(
            fixture.service.createAccount(input, accountNo),
            Status::INVALID_INPUT
        );
        CHECK(accountNo.empty());
    }
}

void test_customer_and_account_number_sequences() {
    struct Seed {
        const char* customerId;
        const char* accountNo;
        const char* expectedCustomerId;
        const char* expectedAccountNo;
    };
    const std::array<Seed, 4> seeds = {{
        {"CUST0009", "BMS000009", "CUST0010", "BMS000010"},
        {"CUST0099", "BMS000099", "CUST0100", "BMS000100"},
        {"CUST0999", "BMS000999", "CUST1000", "BMS001000"},
        {"CUSTinvalid", "BMSinvalid", "CUST0001", "BMS000001"}
    }};

    for (const Seed& seed : seeds) {
        Fixture fixture;
        const Customer customer{
            seed.customerId, "Existing Customer", "9876543210", "City"
        };
        CHECK(fixture.store.addCustomer(customer));

        Account account;
        account.accountNo = seed.accountNo;
        account.customerId = seed.customerId;
        CHECK(fixture.store.addAccount(account));

        std::string accountNo;
        CHECK_EQ(
            fixture.service.createAccount(validInfo(), accountNo),
            Status::OK
        );
        CHECK_EQ(accountNo, seed.expectedAccountNo);
        CHECK(fixture.credentials.load());
        CHECK(fixture.credentials.find(seed.expectedCustomerId) != nullptr);
    }
}

void test_storage_and_credential_failure_paths() {
    {
        Fixture fixture;
        bms::test::writeFile(
            fixture.dir.file("customers.dat"), "#VERSION invalid\n"
        );
        std::string accountNo;
        CHECK_EQ(
            fixture.service.createAccount(validInfo(), accountNo),
            Status::STORAGE_FAILURE
        );
        CHECK(accountNo.empty());
    }

    {
        Fixture fixture;
        bms::test::writeFile(fixture.dir.file("credentials.dat"), "bad\n");
        std::string accountNo;
        CHECK_EQ(
            fixture.service.createAccount(validInfo(), accountNo),
            Status::STORAGE_FAILURE
        );
        CHECK(accountNo.empty());
    }

    {
        Fixture fixture;
        Credential credential;
        credential.userId = "CUST0001";
        fixture.credentials.upsert(credential);
        CHECK(fixture.credentials.save());

        std::string accountNo;
        CHECK_EQ(
            fixture.service.createAccount(validInfo(), accountNo),
            Status::STORAGE_FAILURE
        );
        CHECK(accountNo.empty());
    }

    {
        Fixture fixture;
        std::filesystem::create_directory(
            fixture.dir.file("credentials.dat.tmp")
        );

        std::string accountNo;
        CHECK_EQ(
            fixture.service.createAccount(validInfo(), accountNo),
            Status::STORAGE_FAILURE
        );
        CHECK(accountNo.empty());
        CHECK_EQ(fixture.store.customers().size(), 1u);
        CHECK_EQ(fixture.store.accounts().size(), 1u);
    }
}

} // namespace

int main() {
    test_input_validation_edges();
    test_customer_and_account_number_sequences();
    test_storage_and_credential_failure_paths();
    return bms::test::summary("test_account_service_edges");
}
