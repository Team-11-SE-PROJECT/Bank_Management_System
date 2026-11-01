#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "domain/Account.h"
#include "domain/Customer.h"
#include "domain/Status.h"
#include "infra/CustomerAccountStore.h"
#include "infra/LoginService.h"
#include "service/CustomerSearchService.h"
#include "service/SessionManager.h"

int main() {
    namespace fs = std::filesystem;

    const fs::path testDir =
        fs::temp_directory_path() / "bms_customer_search_service_test";

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
            "Pratheek Kumar",
            "9876543210",
            "Bengaluru"
        };

        const bms::domain::Customer customer2{
            "CUST0002",
            "Rahul Sharma",
            "9876543211",
            "Mysuru"
        };

        if (!store.addCustomer(customer1) ||
            !store.addCustomer(customer2)) {
            std::cerr << "Adding test customers failed\n";
            fs::remove_all(testDir, ec);
            return 1;
        }

        const bms::domain::Account account1{
            "ACC1000001",
            "CUST0001",
            500000,
            "ACTIVE",
            0,
            ""
        };

        const bms::domain::Account account2{
            "ACC1000002",
            "CUST0001",
            250000,
            "ACTIVE",
            0,
            ""
        };

        const bms::domain::Account account3{
            "ACC1000003",
            "CUST0002",
            100000,
            "ACTIVE",
            0,
            ""
        };

        if (!store.addAccount(account1) ||
            !store.addAccount(account2) ||
            !store.addAccount(account3)) {
            std::cerr << "Adding test accounts failed\n";
            fs::remove_all(testDir, ec);
            return 1;
        }

        bms::service::CustomerSearchService searchService(store);

        bms::service::SessionManager adminSession;
        adminSession.login(
            "ADMIN001",
            bms::infra::Role::ADMIN,
            300
        );

        std::vector<bms::service::CustomerSearchRecord> results;

        // Test: Search by exact customer ID.
        if (searchService.search(
                adminSession,
                bms::service::CustomerSearchField::CUSTOMER_ID,
                "CUST0001",
                results
            ) != bms::domain::Status::OK ||
            results.size() != 1 ||
            results[0].customer.customerId != "CUST0001" ||
            results[0].accounts.size() != 2) {
            std::cerr << "Customer ID search failed\n";
            fs::remove_all(testDir, ec);
            return 1;
        }

        // Test: Case-insensitive partial customer name search.
        if (searchService.search(
                adminSession,
                bms::service::CustomerSearchField::CUSTOMER_NAME,
                "PRATh",
                results
            ) != bms::domain::Status::OK ||
            results.size() != 1 ||
            results[0].customer.name != "Pratheek Kumar") {
            std::cerr << "Partial name search failed\n";
            fs::remove_all(testDir, ec);
            return 1;
        }

        // Test: Search by exact phone number.
        if (searchService.search(
                adminSession,
                bms::service::CustomerSearchField::PHONE,
                "9876543211",
                results
            ) != bms::domain::Status::OK ||
            results.size() != 1 ||
            results[0].customer.customerId != "CUST0002") {
            std::cerr << "Phone number search failed\n";
            fs::remove_all(testDir, ec);
            return 1;
        }

        // Test: Search by exact account number.
        if (searchService.search(
                adminSession,
                bms::service::CustomerSearchField::ACCOUNT_NUMBER,
                "ACC1000003",
                results
            ) != bms::domain::Status::OK ||
            results.size() != 1 ||
            results[0].customer.customerId != "CUST0002" ||
            results[0].accounts.size() != 1 ||
            results[0].accounts[0].accountNo != "ACC1000003") {
            std::cerr << "Account number search failed\n";
            fs::remove_all(testDir, ec);
            return 1;
        }

        // Test: A search with no matching record returns NOT_FOUND.
        if (searchService.search(
                adminSession,
                bms::service::CustomerSearchField::CUSTOMER_ID,
                "UNKNOWN",
                results
            ) != bms::domain::Status::NOT_FOUND ||
            !results.empty()) {
            std::cerr << "Missing record search handling failed\n";
            fs::remove_all(testDir, ec);
            return 1;
        }

        // Test: Empty search input is rejected.
        if (searchService.search(
                adminSession,
                bms::service::CustomerSearchField::CUSTOMER_NAME,
                "",
                results
            ) != bms::domain::Status::INVALID_INPUT) {
            std::cerr << "Empty search input was accepted\n";
            fs::remove_all(testDir, ec);
            return 1;
        }

        // Test: A customer cannot use the admin search service.
        bms::service::SessionManager customerSession;
        customerSession.login(
            "CUST0001",
            bms::infra::Role::CUSTOMER,
            300
        );

        if (searchService.search(
                customerSession,
                bms::service::CustomerSearchField::CUSTOMER_ID,
                "CUST0001",
                results
            ) != bms::domain::Status::UNAUTHORIZED) {
            std::cerr << "Customer was allowed to use admin search\n";
            fs::remove_all(testDir, ec);
            return 1;
        }

        // Test: A logged-out user cannot search.
        bms::service::SessionManager loggedOutSession;

        if (searchService.search(
                loggedOutSession,
                bms::service::CustomerSearchField::CUSTOMER_ID,
                "CUST0001",
                results
            ) != bms::domain::Status::UNAUTHORIZED) {
            std::cerr << "Logged-out user was allowed to search\n";
            fs::remove_all(testDir, ec);
            return 1;
        }
    }

    fs::remove_all(testDir, ec);

    std::cout << "test_customer_search_service: all checks passed\n";
    return 0;
}