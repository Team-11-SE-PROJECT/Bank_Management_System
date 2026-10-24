#include <filesystem>
#include <iostream>
#include <string>

#include "domain/Status.h"
#include "infra/CredentialStore.h"
#include "infra/CustomerAccountStore.h"
#include "infra/PasswordHasher.h"
#include "service/AccountService.h"

int main() {
    const std::string testDir = "account_service_test_data";
    std::filesystem::remove_all(testDir);
    std::filesystem::create_directories(testDir);

    bms::infra::CustomerAccountStore dataStore(
        testDir + "/customers.dat",
        testDir + "/accounts.dat"
    );

    bms::infra::CredentialStore credentialStore(
        testDir + "/credentials.dat"
    );

    bms::service::AccountService service(dataStore, credentialStore);

    bms::service::CustomerInfo info{
        "Test Customer",
        "9876543210",
        "Bengaluru",
        "1234"
    };

    std::string accountNo;
    auto status = service.createAccount(info, accountNo);

    if (status != bms::domain::Status::OK || accountNo.empty()) {
        std::cerr << "FAIL: valid registration failed\n";
        std::filesystem::remove_all(testDir);
        return 1;
    }

    if (!dataStore.load() ||
        dataStore.customers().size() != 1 ||
        dataStore.accounts().size() != 1) {
        std::cerr << "FAIL: customer or account was not saved\n";
        std::filesystem::remove_all(testDir);
        return 1;
    }

    if (!credentialStore.load()) {
        std::cerr << "FAIL: credentials could not be loaded\n";
        std::filesystem::remove_all(testDir);
        return 1;
    }

    const auto* credential = credentialStore.find("CUST0001");

    if (credential == nullptr ||
        !bms::infra::PasswordHasher::verify(
            "1234", credential->salt, credential->hashHex)) {
        std::cerr << "FAIL: customer PIN credential is invalid\n";
        std::filesystem::remove_all(testDir);
        return 1;
    }

    bms::service::CustomerInfo invalidPhoneInfo{
        "Invalid Phone Customer",
        "123",
        "Bengaluru",
        "1234"
    };

    std::string invalidPhoneAccountNo;
    status = service.createAccount(
        invalidPhoneInfo, invalidPhoneAccountNo
    );

    if (status != bms::domain::Status::INVALID_INPUT) {
        std::cerr << "FAIL: invalid phone number was accepted\n";
        std::filesystem::remove_all(testDir);
        return 1;
    }

    bms::service::CustomerInfo invalidPinInfo{
        "Invalid PIN Customer",
        "9123456780",
        "Bengaluru",
        "12AB"
    };

    std::string invalidPinAccountNo;
    status = service.createAccount(
        invalidPinInfo, invalidPinAccountNo
    );

    if (status != bms::domain::Status::INVALID_INPUT) {
        std::cerr << "FAIL: invalid PIN was accepted\n";
        std::filesystem::remove_all(testDir);
        return 1;
    }

    std::filesystem::remove_all(testDir);

    std::cout << "test_account_service: all checks passed\n";
    return 0;
}
