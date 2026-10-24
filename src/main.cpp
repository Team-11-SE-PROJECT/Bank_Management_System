#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>

#include "infra/AccountRepository.h"
#include "infra/AuditLogger.h"
#include "infra/Config.h"
#include "infra/CredentialStore.h"
#include "infra/InputValidator.h"
#include "infra/LoadStatus.h"
#include "infra/LoginService.h"
#include "infra/TransactionRepository.h"
#include "service/TransactionService.h"

namespace {

using bms::service::DepositMode;
using bms::service::TxnResult;
using bms::service::TxnStatus;

bool login(
    bms::infra::LoginService& service,
    bms::infra::Role role,
    std::string& userIdOut
) {
    std::string userId;
    std::string password;

    std::cout << "User ID: ";
    if (!std::getline(std::cin, userId)) return false;

    std::cout << "Password: ";
    if (!std::getline(std::cin, password)) return false;

    const auto result = service.authenticate(userId, password, role);

    switch (result.status) {
        case bms::infra::LoginStatus::SUCCESS:
            std::cout << "Login successful. Welcome, "
                      << userId << ".\n";
            userIdOut = userId;
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

template <typename Repository>
bool loadOrReport(Repository& repo, const char* path) {
    const auto status = repo.load();

    if (status == bms::infra::LoadStatus::CORRUPT) {
        std::cerr << "Error loading " << path << ": "
                  << repo.lastError() << '\n';
        return false;
    }

    if (status == bms::infra::LoadStatus::MISSING) {
        std::cout << "Note: " << path
                  << " does not exist yet; starting with no records.\n";
    }

    return true;
}

const char* message(TxnStatus status) {
    switch (status) {
        case TxnStatus::OK:
            return "Success.";
        case TxnStatus::INVALID_INPUT:
            return "Invalid amount. Enter a positive amount with at most 2 decimals.";
        case TxnStatus::NOT_FOUND:
            return "Account not found.";
        case TxnStatus::ACCOUNT_CLOSED:
            return "This account is closed.";
        case TxnStatus::INSUFFICIENT_FUNDS:
            return "Insufficient balance.";
        case TxnStatus::LIMIT_EXCEEDED:
            return "Daily withdrawal limit exceeded.";
        case TxnStatus::STORAGE_FAILURE:
            return "Storage error. Please check the data files.";
    }

    return "Unknown error.";
}

std::string rupees(long long paise) {
    return "Rs. " +
        bms::infra::InputValidator::formatPaise(paise);
}

void showResult(const TxnResult& result) {
    if (result.status != TxnStatus::OK) {
        std::cout << message(result.status) << '\n';
        return;
    }

    if (!result.txnId.empty()) {
        std::cout << "Transaction ID: " << result.txnId << '\n';
    }

    std::cout << "Current balance: "
              << rupees(result.newBalancePaise) << '\n';
}

void customerMenu(
    const std::string& userId,
    bms::infra::AccountRepository& accounts,
    bms::service::TransactionService& service
) {
    // The project design uses the login user ID as customerId.
    const auto* account = accounts.findByCustomer(userId);

    if (!account) {
        std::cout << "No bank account is linked to this user.\n";
        return;
    }

    // Copy it because repository records can be updated by transactions.
    const std::string accountNo = account->accountNo;

    while (true) {
        std::cout << "\n--- Customer Banking Menu ---\n"
                  << "Account: " << accountNo << '\n'
                  << "1. Check balance\n"
                  << "2. Deposit\n"
                  << "3. Withdraw\n"
                  << "0. Logout\n"
                  << "Choice: ";

        std::string choice;
        if (!std::getline(std::cin, choice)) return;

        if (choice == "0") return;

        if (choice == "1") {
            showResult(service.getBalance(accountNo));
            continue;
        }

        if (choice != "2" && choice != "3") {
            std::cout << "Invalid choice.\n";
            continue;
        }

        std::string amountText;
        std::cout << "Amount in rupees: ";

        if (!std::getline(std::cin, amountText)) return;

        long long amountPaise = 0;

        if (!bms::infra::InputValidator::parseAmountPaise(
                amountText, amountPaise)) {
            std::cout << message(TxnStatus::INVALID_INPUT) << '\n';
            continue;
        }

        TxnResult result;

        if (choice == "2") {
            std::cout << "Deposit mode (1 = Cash, 2 = Cheque): ";

            std::string mode;
            if (!std::getline(std::cin, mode)) return;

            if (mode != "1" && mode != "2") {
                std::cout << "Invalid deposit mode.\n";
                continue;
            }

            result = service.deposit(
                accountNo,
                amountPaise,
                mode == "1" ? DepositMode::CASH : DepositMode::CHEQUE
            );
        } else {
            result = service.withdraw(accountNo, amountPaise);
        }

        if (result.status == TxnStatus::OK) {
            std::cout << (choice == "2" ? "Deposit" : "Withdrawal")
                      << " successful.\n";
        }

        showResult(result);
    }
}

} // namespace

int main() {
    std::error_code ec;
    std::filesystem::create_directories("data", ec);

    if (ec) {
        std::cerr << "Cannot create data directory: "
                  << ec.message() << '\n';
        return 1;
    }

    bms::infra::Config config;

    if (config.load("config.ini") ==
        bms::infra::LoadStatus::CORRUPT) {
        std::cerr << "Invalid config.ini: "
                  << config.lastError() << '\n';
        return 1;
    }

    bms::infra::CredentialStore credentials("data/credentials.dat");

    if (!credentials.load()) {
        std::cerr << "Could not load credential data.\n";
        return 1;
    }

    bms::infra::LoginService loginService(credentials);

    bms::infra::AccountRepository accounts("data/accounts.dat");
    bms::infra::TransactionRepository transactions("data/transactions.dat");

    if (!loadOrReport(accounts, "data/accounts.dat") ||
        !loadOrReport(transactions, "data/transactions.dat")) {
        return 1;
    }

    bms::infra::AuditLogger audit("data/audit.log");

    bms::service::TransactionService transactionService(
        accounts, transactions, config, audit
    );

    while (true) {
        std::cout << "\nBank Management System\n"
                  << "1. Customer login\n"
                  << "2. Admin login\n"
                  << "0. Exit\n"
                  << "Selection: ";

        std::string selection;
        if (!std::getline(std::cin, selection)) return 0;

        if (selection == "0") return 0;

        std::string userId;

        if (selection == "1") {
            if (login(
                    loginService,
                    bms::infra::Role::CUSTOMER,
                    userId)) {
                customerMenu(userId, accounts, transactionService);
            }
        } else if (selection == "2") {
            if (login(
                    loginService,
                    bms::infra::Role::ADMIN,
                    userId)) {
                std::cout << "Admin session authenticated.\n";
            }
        } else {
            std::cout << "Invalid selection.\n";
        }
    }
}