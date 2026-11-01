
#include <filesystem>
#include <iostream>
#include <string>

#include "domain/Account.h"
#include "domain/Customer.h"
#include "infra/CustomerAccountStore.h"

int main() {
    namespace fs = std::filesystem;

    const fs::path testDir =
        fs::temp_directory_path() / "bms_customer_update_test";

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
            "Original Customer",
            "9876543210",
            "Bengaluru"
        };

        if (!store.addCustomer(customer)) {
            std::cerr << "Adding customer failed\n";
            fs::remove_all(testDir, ec);
            return 1;
        }

        bms::domain::Account account;
        account.accountNo = "BMS000001";
        account.customerId = "CUST0001";
        account.balancePaise = 100000;
        account.status = "ACTIVE";

        if (!store.addAccount(account)) {
            std::cerr << "Adding account failed\n";
            fs::remove_all(testDir, ec);
            return 1;
        }

        // Test successful customer update.
        bms::domain::Customer updatedCustomer{
            "CUST0001",
            "Updated Customer",
            "9123456780",
            "Electronic City, Bengaluru"
        };

        if (!store.updateCustomer(updatedCustomer)) {
            std::cerr << "Customer update failed\n";
            fs::remove_all(testDir, ec);
            return 1;
        }

        const auto* updated = store.findCustomer("CUST0001");

        if (updated == nullptr ||
            updated->name != "Updated Customer" ||
            updated->phone != "9123456780" ||
            updated->address != "Electronic City, Bengaluru") {
            std::cerr << "Updated customer information is incorrect\n";
            fs::remove_all(testDir, ec);
            return 1;
        }

        // Test that an unknown customer cannot be updated.
        bms::domain::Customer unknownCustomer{
            "CUST9999",
            "Unknown Customer",
            "9123456780",
            "Bengaluru"
        };

        if (store.updateCustomer(unknownCustomer)) {
            std::cerr << "Unknown customer update was accepted\n";
            fs::remove_all(testDir, ec);
            return 1;
        }

        // Test invalid phone number.
        bms::domain::Customer invalidPhone{
            "CUST0001",
            "Invalid Phone",
            "12345",
            "Bengaluru"
        };

        if (store.updateCustomer(invalidPhone)) {
            std::cerr << "Invalid phone number was accepted\n";
            fs::remove_all(testDir, ec);
            return 1;
        }

        // Test invalid storage delimiter.
        bms::domain::Customer invalidName{
            "CUST0001",
            "Invalid|Customer",
            "9123456780",
            "Bengaluru"
        };

        if (store.updateCustomer(invalidName)) {
            std::cerr << "Customer name with delimiter was accepted\n";
            fs::remove_all(testDir, ec);
            return 1;
        }

        // Verify failed updates did not change existing information.
        updated = store.findCustomer("CUST0001");

        if (updated == nullptr ||
            updated->name != "Updated Customer" ||
            updated->phone != "9123456780") {
            std::cerr << "Failed update changed customer information\n";
            fs::remove_all(testDir, ec);
            return 1;
        }

        // Verify account ownership and balance remain unchanged.
        const auto* savedAccount = store.findAccount("BMS000001");

        if (savedAccount == nullptr ||
            savedAccount->customerId != "CUST0001" ||
            savedAccount->balancePaise != 100000) {
            std::cerr << "Customer update affected account information\n";
            fs::remove_all(testDir, ec);
            return 1;
        }
    }

    // Verify customer and account data after reloading from files.
    {
        bms::infra::CustomerAccountStore store(
            customersPath,
            accountsPath
        );

        if (!store.load()) {
            std::cerr << "Reload after update failed\n";
            fs::remove_all(testDir, ec);
            return 1;
        }

        const auto* customer = store.findCustomer("CUST0001");
        const auto* account = store.findAccount("BMS000001");

        if (customer == nullptr ||
            customer->customerId != "CUST0001" ||
            customer->name != "Updated Customer" ||
            customer->phone != "9123456780" ||
            customer->address != "Electronic City, Bengaluru") {
            std::cerr << "Updated customer data was not persisted\n";
            fs::remove_all(testDir, ec);
            return 1;
        }

        if (account == nullptr ||
            account->customerId != "CUST0001" ||
            account->balancePaise != 100000) {
            std::cerr << "Account data was not preserved after reload\n";
            fs::remove_all(testDir, ec);
            return 1;
        }
    }

    fs::remove_all(testDir, ec);

    std::cout << "test_customer_update: all checks passed\n";
    return 0;
}
