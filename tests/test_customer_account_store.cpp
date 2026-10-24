
#include <filesystem>
#include <iostream>
#include <string>

#include "domain/Account.h"
#include "domain/Customer.h"
#include "infra/CustomerAccountStore.h"

int main() {
    namespace fs = std::filesystem;
    namespace bms = bms;

    const fs::path testDir =
        fs::temp_directory_path() / "bms_customer_account_store_test";

    std::error_code ec;
    fs::remove_all(testDir, ec);
    fs::create_directories(testDir);

    const std::string customersPath =
        (testDir / "customers.dat").string();
    const std::string accountsPath =
        (testDir / "accounts.dat").string();

    {
        bms::infra::CustomerAccountStore store(
            customersPath,
            accountsPath
        );

        if (!store.load()) {
            std::cerr << "Initial load failed\n";
            return 1;
        }

        bms::domain::Customer customer{
            "CUST0001",
            "Test Customer",
            "9876543210",
            "Bengaluru"
        };

        if (!store.addCustomer(customer)) {
            std::cerr << "Adding customer failed\n";
            return 1;
        }

        if (store.addCustomer(customer)) {
            std::cerr << "Duplicate customer was accepted\n";
            return 1;
        }

        bms::domain::Account account;
        account.accountNo = "BMS000001";
        account.customerId = "CUST0001";
        account.balancePaise = 10000;
        account.status = "ACTIVE";

        if (!store.addAccount(account)) {
            std::cerr << "Adding account failed\n";
            return 1;
        }

        if (store.addAccount(account)) {
            std::cerr << "Duplicate account was accepted\n";
            return 1;
        }
    }

    {
        bms::infra::CustomerAccountStore store(
            customersPath,
            accountsPath
        );

        if (!store.load()) {
            std::cerr << "Reload failed\n";
            return 1;
        }

        if (store.customers().size() != 1 ||
            store.accounts().size() != 1) {
            std::cerr << "Saved records did not reload correctly\n";
            return 1;
        }

        if (store.accounts()[0].balancePaise != 10000) {
            std::cerr << "Account balance was not preserved\n";
            return 1;
        }
    }

    fs::remove_all(testDir, ec);

    std::cout << "test_customer_account_store: all checks passed\n";
    return 0;
}
