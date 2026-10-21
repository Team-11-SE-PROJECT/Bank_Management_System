#include <iostream>
#include <string>

#include "infra/CredentialStore.h"
#include "infra/LoginService.h"

namespace {

bool login(bms::infra::LoginService& service, bms::infra::Role role) {
    std::string userId;
    std::string password;
    std::cout << "User ID: ";
    if (!std::getline(std::cin, userId)) return false;
    std::cout << "Password: ";
    if (!std::getline(std::cin, password)) return false;

    const auto result = service.authenticate(userId, password, role);
    switch (result.status) {
        case bms::infra::LoginStatus::SUCCESS:
            std::cout << "Login successful. Welcome, " << userId << ".\n";
            return true;
        case bms::infra::LoginStatus::INVALID_CREDENTIALS:
            std::cout << "Invalid user ID or password. Attempts remaining: "
                      << result.attemptsRemaining << ".\n";
            return false;
        case bms::infra::LoginStatus::ACCOUNT_LOCKED:
            std::cout << "Account locked after too many failed attempts.\n";
            return false;
        case bms::infra::LoginStatus::STORAGE_ERROR:
            std::cerr << "Could not save login state. Please try again later.\n";
            return false;
    }
    return false;
}

} // namespace

int main() {
    bms::infra::CredentialStore store("data/credentials.dat");
    if (!store.load()) {
        std::cerr << "Could not load credential data.\n";
        return 1;
    }
    bms::infra::LoginService service(store);

    while (true) {
        std::cout << "\nBank Management System\n"
                     "1. Customer login\n"
                     "2. Admin login\n"
                     "0. Exit\n"
                     "Selection: ";
        std::string selection;
        if (!std::getline(std::cin, selection)) return 0;

        if (selection == "0") return 0;
        if (selection == "1") {
            if (login(service, bms::infra::Role::CUSTOMER))
                std::cout << "Customer session authenticated.\n";
        } else if (selection == "2") {
            if (login(service, bms::infra::Role::ADMIN))
                std::cout << "Admin session authenticated.\n";
        } else {
            std::cout << "Invalid selection.\n";
        }
    }
}