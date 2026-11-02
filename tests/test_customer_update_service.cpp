#include <filesystem>
#include <iostream>
#include <string>

#include "domain/Customer.h"
#include "domain/Status.h"
#include "infra/CustomerAccountStore.h"
#include "infra/CredentialStore.h"
#include "service/CustomerUpdateService.h"
#include "service/SessionManager.h"

int main() {
    namespace fs = std::filesystem;

    const fs::path testDir =
        fs::temp_directory_path() / "bms_customer_update_service_test";

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
            std::cerr << "Initial store load failed\n";
            fs::remove_all(testDir, ec);
            return 1;
        }

        const bms::domain::Customer customer1{
            "CUST0001",
            "Customer One",
            "9876543210",
            "Bengaluru"
        };

        const bms::domain::Customer customer2{
            "CUST0002",
            "Customer Two",
            "9876543211",
            "Mysuru"
        };

        if (!store.addCustomer(customer1) ||
            !store.addCustomer(customer2)) {
            std::cerr << "Adding test customers failed\n";
            fs::remove_all(testDir, ec);
            return 1;
        }

        bms::service::CustomerUpdateService updateService(store);

        // Test: A customer can update their own information.
        bms::service::SessionManager customerSession;
        customerSession.login(
            "CUST0001",
            bms::infra::Role::CUSTOMER,
            300
        );

        const bms::domain::Customer updatedCustomer1{
            "CUST0001",
            "Updated Customer One",
            "9123456780",
            "Electronic City, Bengaluru"
        };

        if (updateService.updateCustomer(
                customerSession,
                updatedCustomer1
            ) != bms::domain::Status::OK) {
            std::cerr << "Customer could not update their own information\n";
            fs::remove_all(testDir, ec);
            return 1;
        }

        // Test: A customer cannot update another customer's information.
        const bms::domain::Customer unauthorizedUpdate{
            "CUST0002",
            "Unauthorized Change",
            "9123456782",
            "Bengaluru"
        };

        if (updateService.updateCustomer(
                customerSession,
                unauthorizedUpdate
            ) != bms::domain::Status::UNAUTHORIZED) {
            std::cerr << "Customer updated another customer's information\n";
            fs::remove_all(testDir, ec);
            return 1;
        }

        const auto* customer2AfterAttempt =
            store.findCustomer("CUST0002");

        if (customer2AfterAttempt == nullptr ||
            customer2AfterAttempt->name != "Customer Two") {
            std::cerr << "Unauthorized update changed customer information\n";
            fs::remove_all(testDir, ec);
            return 1;
        }

        // Test: An administrator can update another customer's information.
        bms::service::SessionManager adminSession;
        adminSession.login(
            "ADMIN001",
            bms::infra::Role::ADMIN,
            300
        );

        const bms::domain::Customer adminUpdate{
            "CUST0002",
            "Admin Updated Customer",
            "9988776655",
            "Electronic City, Bengaluru"
        };

        if (updateService.updateCustomer(
                adminSession,
                adminUpdate
            ) != bms::domain::Status::OK) {
            std::cerr << "Administrator could not update customer information\n";
            fs::remove_all(testDir, ec);
            return 1;
        }

        // Test: A logged-out user cannot update customer information.
        bms::service::SessionManager loggedOutSession;

        if (updateService.updateCustomer(
                loggedOutSession,
                updatedCustomer1
            ) != bms::domain::Status::UNAUTHORIZED) {
            std::cerr << "Logged-out user was allowed to update information\n";
            fs::remove_all(testDir, ec);
            return 1;
        }

        // Test: Empty customer information is rejected.
        const bms::domain::Customer invalidCustomer{
            "CUST0001",
            "",
            "9123456780",
            "Bengaluru"
        };

        if (updateService.updateCustomer(
                customerSession,
                invalidCustomer
            ) != bms::domain::Status::INVALID_INPUT) {
            std::cerr << "Empty customer information was accepted\n";
            fs::remove_all(testDir, ec);
            return 1;
        }
    }

    // Verify that updates remain saved after reloading the data files.
    {
        bms::infra::CustomerAccountStore store(
            customersPath,
            accountsPath
        );

        if (!store.load()) {
            std::cerr << "Reload after updates failed\n";
            fs::remove_all(testDir, ec);
            return 1;
        }

        const auto* customer1 = store.findCustomer("CUST0001");
        const auto* customer2 = store.findCustomer("CUST0002");

        if (customer1 == nullptr ||
            customer1->name != "Updated Customer One" ||
            customer1->phone != "9123456780" ||
            customer1->address != "Electronic City, Bengaluru") {
            std::cerr << "Customer One update was not persisted\n";
            fs::remove_all(testDir, ec);
            return 1;
        }

        if (customer2 == nullptr ||
            customer2->name != "Admin Updated Customer" ||
            customer2->phone != "9988776655" ||
            customer2->address != "Electronic City, Bengaluru") {
            std::cerr << "Administrator update was not persisted\n";
            fs::remove_all(testDir, ec);
            return 1;
        }
    }

    fs::remove_all(testDir, ec);

    std::cout << "test_customer_update_service: all checks passed\n";
    return 0;
}