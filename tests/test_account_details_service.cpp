
#include <filesystem>
#include <iostream>
#include <string>

#include "infra/InputValidator.h"
#include "domain/Account.h"
#include "domain/Customer.h"
#include "infra/CustomerAccountStore.h"

int main() {
    namespace fs = std::filesystem;

    const fs::path testDir =
        fs::temp_directory_path() / "bms_customer_account_store_test";

    std::error_code ec;
    fs::remove_all(testDir, ec);

    if (!fs::create_directories(testDir, ec) && ec) {
        std::cerr << "Could not create test directory\n";
        return 1;
    }

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
            fs::remove_all(testDir, ec);
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
            fs::remove_all(testDir, ec);
            return 1;
        }

        if (store.addCustomer(customer)) {
            std::cerr << "Duplicate customer was accepted\n";
            fs::remove_all(testDir, ec);
            return 1;
        }

        bms::domain::Account account;
        account.accountNo = "BMS000001";
        account.customerId = "CUST0001";
        account.balancePaise = 10000;
        account.status = "ACTIVE";

        if (!store.addAccount(account)) {
            std::cerr << "Adding account failed\n";
            fs::remove_all(testDir, ec);
            return 1;
        }

        if (store.addAccount(account)) {
            std::cerr << "Duplicate account was accepted\n";
            fs::remove_all(testDir, ec);
            return 1;
        }

        bms::domain::Account invalidBalance = account;
        invalidBalance.accountNo = "BMS000002";
        invalidBalance.balancePaise = -1;

        if (store.addAccount(invalidBalance)) {
            std::cerr << "Negative balance was accepted\n";
            fs::remove_all(testDir, ec);
            return 1;
        }

        bms::domain::Account excessiveBalance = account;
        excessiveBalance.accountNo = "BMS000003";
        excessiveBalance.balancePaise =
            bms::infra::kMaxAmountPaise + 1;

        if (store.addAccount(excessiveBalance)) {
            std::cerr << "Excessive balance was accepted\n";
            fs::remove_all(testDir, ec);
            return 1;
        }

        bms::domain::Account invalidStatus = account;
        invalidStatus.accountNo = "BMS000004";
        invalidStatus.status = "FROZEN";

        if (store.addAccount(invalidStatus)) {
            std::cerr << "Invalid account status was accepted\n";
            fs::remove_all(testDir, ec);
            return 1;
        }

        bms::domain::Account invalidWithdrawal = account;
        invalidWithdrawal.accountNo = "BMS000005";
        invalidWithdrawal.withdrawnTodayPaise = -1;

        if (store.addAccount(invalidWithdrawal)) {
            std::cerr << "Negative daily withdrawal was accepted\n";
            fs::remove_all(testDir, ec);
            return 1;
        }

        bms::domain::Account unknownCustomer = account;
        unknownCustomer.accountNo = "BMS000006";
        unknownCustomer.customerId = "CUST9999";

        if (store.addAccount(unknownCustomer)) {
            std::cerr << "Account with unknown customer was accepted\n";
            fs::remove_all(testDir, ec);
            return 1;
        }

        bms::domain::Customer invalidCustomer{
            "CUST0002",
            "",
            "9876543210",
            "Bengaluru"
        };

        if (store.addCustomer(invalidCustomer)) {
            std::cerr << "Customer with empty name was accepted\n";
            fs::remove_all(testDir, ec);
            return 1;
        }

        bms::domain::Customer invalidDelimiterCustomer{
            "CUST0002",
            "Test|Customer",
            "9876543210",
            "Bengaluru"
        };

        if (store.addCustomer(invalidDelimiterCustomer)) {
            std::cerr << "Customer containing storage delimiter was accepted\n";
            fs::remove_all(testDir, ec);
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
            fs::remove_all(testDir, ec);
            return 1;
        }

        if (store.customers().size() != 1 ||
            store.accounts().size() != 1) {
            std::cerr << "Saved records did not reload correctly\n";
            fs::remove_all(testDir, ec);
            return 1;
        }

        if (store.accounts()[0].balancePaise != 10000) {
            std::cerr << "Account balance was not preserved\n";
            fs::remove_all(testDir, ec);
            return 1;
        }

        if (store.findCustomer("CUST0001") == nullptr ||
            store.findAccount("BMS000001") == nullptr) {
            std::cerr << "Record lookup failed\n";
            fs::remove_all(testDir, ec);
            return 1;
        }

        if (store.findCustomer("CUST9999") != nullptr ||
            store.findAccount("BMS999999") != nullptr) {
            std::cerr << "Missing record lookup returned a result\n";
            fs::remove_all(testDir, ec);
            return 1;
        }
    }

    fs::remove_all(testDir, ec);

    std::cout << "test_customer_account_store: all checks passed\n";
    return 0;
}
