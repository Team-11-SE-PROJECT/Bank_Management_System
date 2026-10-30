#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>

#ifdef _WIN32
#include <conio.h>
#include <io.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

#include "domain/Status.h"
#include "infra/AccountRepository.h"
#include "infra/AuditLogger.h"
#include "infra/Config.h"
#include "infra/CredentialStore.h"
#include "infra/CustomerAccountStore.h"
#include "infra/InputValidator.h"
#include "infra/Journal.h"
#include "infra/LoadStatus.h"
#include "infra/LoginService.h"
#include "infra/TransactionRepository.h"
#include "service/AccountDetailsService.h"
#include "service/AccountService.h"
#include "service/AccountUnlockService.h"
#include "service/AuthService.h"
#include "service/SessionManager.h"
#include "service/SystemStatusService.h"
#include "service/TransactionService.h"

namespace {

using bms::service::DepositMode;
using bms::service::TxnResult;
using bms::service::TxnStatus;

bool readMaskedPassword(std::string& password) {
#ifdef _WIN32
    if (!_isatty(_fileno(stdin))) {
        return static_cast<bool>(std::getline(std::cin, password));
    }

    while (true) {
        const int ch = _getch();
        if (ch == 0 || ch == 224) {
            _getch();
            continue;
        }
        if (ch == '\r') {
            std::cout << '\n';
            return true;
        }
        if (ch == '\b') {
            if (!password.empty()) {
                password.pop_back();
                std::cout << "\b \b" << std::flush;
            }
            continue;
        }
        if (ch == 3 || ch == 26) {
            std::cout << '\n';
            return false;
        }
        if (ch >= 32 && ch <= 126) {
            password.push_back(static_cast<char>(ch));
            std::cout << '*' << std::flush;
        }
    }
#else
    if (!isatty(STDIN_FILENO)) {
        return static_cast<bool>(std::getline(std::cin, password));
    }

    termios original{};
    if (tcgetattr(STDIN_FILENO, &original) != 0) {
        return static_cast<bool>(std::getline(std::cin, password));
    }

    termios masked = original;
    masked.c_lflag &= static_cast<tcflag_t>(~(ECHO | ICANON));
    if (tcsetattr(STDIN_FILENO, TCSANOW, &masked) != 0) {
        return static_cast<bool>(std::getline(std::cin, password));
    }

    bool complete = false;
    while (!complete) {
        const int ch = std::getchar();
        if (ch == EOF || ch == 3 || ch == 4) {
            break;
        }
        if (ch == '\n' || ch == '\r') {
            complete = true;
        } else if (ch == 127 || ch == '\b') {
            if (!password.empty()) {
                password.pop_back();
                std::cout << "\b \b" << std::flush;
            }
        } else if (ch >= 32 && ch <= 126) {
            password.push_back(static_cast<char>(ch));
            std::cout << '*' << std::flush;
        }
    }

    if (tcsetattr(STDIN_FILENO, TCSANOW, &original) != 0) {
        std::cerr << "Could not restore terminal input settings.\n";
        return false;
    }
    if (complete) {
        std::cout << '\n';
    }
    return complete;
#endif
}

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
    if (!readMaskedPassword(password)) return false;

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

bool prompt(const char* label, std::string& value) {
    std::cout << label;
    return static_cast<bool>(std::getline(std::cin, value));
}

void showDomainStatus(bms::domain::Status status) {
    using bms::domain::Status;

    switch (status) {
        case Status::OK:
            std::cout << "Operation successful.\n";
            break;
        case Status::UNAUTHORIZED:
            std::cout << "Access denied.\n";
            break;
        case Status::INVALID_INPUT:
            std::cout << "Invalid input. Please check the entered values.\n";
            break;
        case Status::NOT_FOUND:
            std::cout << "User or record not found.\n";
            break;
        case Status::STORAGE_FAILURE:
            std::cout << "Storage error. Please check the data files.\n";
            break;
    }
}

bool changePin(
    const std::string& userId,
    bms::service::AuthService& service
) {
    std::string oldPin;
    std::string newPin;

    std::cout << "Current PIN: ";
    if (!readMaskedPassword(oldPin)) return false;
    std::cout << "New PIN (4-6 digits): ";
    if (!readMaskedPassword(newPin)) return false;

    const auto status = service.changeCredentials(userId, oldPin, newPin);
    if (status == bms::domain::Status::OK) {
        std::cout << "PIN changed successfully.\n";
    } else if (status == bms::domain::Status::UNAUTHORIZED) {
        std::cout << "Current PIN is incorrect.\n";
    } else {
        showDomainStatus(status);
    }
    return true;
}

void showAccountDetails(
    const std::string& userId,
    const std::string& accountNo,
    bms::infra::CustomerAccountStore& customerData
) {
    if (!customerData.load()) {
        std::cerr << "Could not load customer and account details.\n";
        return;
    }

    const bms::service::AccountDetailsService service(customerData);
    const auto result = service.getByAccountNumber(accountNo, userId);

    switch (result.status) {
        case bms::service::AccountDetailsStatus::OK:
            std::cout << "\n--- Account Details ---\n"
                      << "Customer ID: " << result.customer.customerId << '\n'
                      << "Name: " << result.customer.name << '\n'
                      << "Phone: " << result.customer.phone << '\n'
                      << "Address: " << result.customer.address << '\n'
                      << "Account number: " << result.account.accountNo << '\n'
                      << "Account status: " << result.account.status << '\n'
                      << "Balance: " << rupees(result.account.balancePaise)
                      << '\n';
            break;
        case bms::service::AccountDetailsStatus::INVALID_INPUT:
            std::cout << "Account number or customer ID is missing.\n";
            break;
        case bms::service::AccountDetailsStatus::NOT_FOUND:
            std::cout << "Account details not found.\n";
            break;
        case bms::service::AccountDetailsStatus::UNAUTHORIZED:
            std::cout << "You can only view your own account details.\n";
            break;
    }
}

void customerMenu(
    const std::string& userId,
    bms::infra::AccountRepository& accounts,
    bms::service::TransactionService& service,
    bms::infra::CustomerAccountStore& customerData,
    bms::service::AuthService& authService
) {
    if (!loadOrReport(accounts, "data/accounts.dat")) return;

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
                  << "4. View account details\n"
                  << "5. Change PIN\n"
                  << "0. Logout\n"
                  << "Choice: ";

        std::string choice;
        if (!std::getline(std::cin, choice)) return;

        if (choice == "0") return;

        if (choice == "1") {
            showResult(service.getBalance(accountNo));
            continue;
        }

        if (choice == "4") {
            showAccountDetails(userId, accountNo, customerData);
            continue;
        }

        if (choice == "5") {
            if (!changePin(userId, authService)) return;
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

void adminMenu(
    const std::string& userId,
    bms::service::SessionManager& session,
    bms::service::AccountService& accountService,
    bms::service::AccountUnlockService& unlockService,
    bms::service::SystemStatusService& systemStatusService,
    bms::service::AuthService& authService,
    bms::infra::CredentialStore& credentials,
    bms::infra::CustomerAccountStore& customerData,
    bms::infra::AccountRepository& accounts
) {
    session.login(userId, bms::infra::Role::ADMIN, 300);

    while (true) {
        std::cout << "\n--- Admin Menu ---\n"
                  << "1. Create customer account\n"
                  << "2. Unlock customer login\n"
                  << "3. View system status\n"
                  << "4. Change admin PIN\n"
                  << "0. Logout\n"
                  << "Choice: ";

        std::string choice;
        if (!std::getline(std::cin, choice)) {
            session.logout();
            return;
        }

        if (choice == "0") {
            session.logout();
            return;
        }

        if (choice == "1") {
            bms::service::CustomerInfo info;
            if (!prompt("Customer full name: ", info.name) ||
                !prompt("Phone (10 digits): ", info.phone) ||
                !prompt("Address: ", info.address)) {
                session.logout();
                return;
            }
            std::cout << "Initial customer PIN (4-6 digits): ";
            if (!readMaskedPassword(info.initialPin)) {
                session.logout();
                return;
            }

            std::string accountNo;
            const auto status =
                accountService.createAccount(info, accountNo);
            showDomainStatus(status);
            if (status == bms::domain::Status::OK) {
                if (!loadOrReport(accounts, "data/accounts.dat")) {
                    session.logout();
                    return;
                }
                const auto* account = accounts.find(accountNo);
                if (account == nullptr) {
                    std::cerr << "Created account could not be reloaded.\n";
                    continue;
                }
                std::cout << "Customer login ID: " << account->customerId
                          << '\n'
                          << "Account number: " << accountNo << '\n'
                          << "Opening balance: Rs. 0.00\n";
            }
            continue;
        }

        if (choice == "2") {
            std::string targetUserId;
            if (!prompt("Customer login ID to unlock: ", targetUserId)) {
                session.logout();
                return;
            }
            showDomainStatus(
                unlockService.unlockAccount(session, targetUserId)
            );
            continue;
        }

        if (choice == "3") {
            if (!credentials.load() || !customerData.load()) {
                std::cerr << "Could not read system data files.\n";
                continue;
            }

            bms::service::SystemStatus status;
            const auto result =
                systemStatusService.getStatus(session, status);
            if (result != bms::domain::Status::OK) {
                showDomainStatus(result);
                continue;
            }

            std::cout << "\n--- System Status ---\n"
                      << "Application running: "
                      << (status.applicationRunning ? "Yes" : "No") << '\n'
                      << "Credentials file accessible: "
                      << (status.credentialsFileAccessible ? "Yes" : "No")
                      << '\n'
                      << "Customers file accessible: "
                      << (status.customersFileAccessible ? "Yes" : "No")
                      << '\n'
                      << "Accounts file accessible: "
                      << (status.accountsFileAccessible ? "Yes" : "No")
                      << '\n'
                      << "Credential records: " << status.credentialCount
                      << '\n'
                      << "Customers: " << status.customerCount << '\n'
                      << "Accounts: " << status.accountCount << '\n';
            continue;
        }

        if (choice == "4") {
            if (!changePin(userId, authService)) {
                session.logout();
                return;
            }
            continue;
        }

        std::cout << "Invalid choice.\n";
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

    bms::infra::CustomerAccountStore customerData(
        "data/customers.dat", "data/accounts.dat"
    );
    const auto custStatus = customerData.loadWithStatus();
    if (custStatus == bms::infra::LoadStatus::CORRUPT) {
        std::cerr << "Error loading customer data: "
                  << customerData.lastStatus().errorMessage << "\n";
        return 1;
    }

    bms::infra::AuditLogger audit("data/audit.log");

    bms::infra::Journal journal("data/journal.dat");
    const auto journalStatus = journal.load();
    if (journalStatus == bms::infra::LoadStatus::CORRUPT) {
        std::cerr << "Error loading journal: " << journal.lastError() << "\n";
        return 1;
    }

    int recovered = bms::infra::Journal::recover(journal, accounts, transactions, &audit);
    if (recovered > 0) {
        std::cout << "Crash recovery complete: " << recovered << " journal entries reconciled.\n";
    }

    bms::service::TransactionService transactionService(
        accounts, transactions, config, audit
    );
    transactionService.setJournal(&journal);
    bms::service::AccountService accountService(customerData, credentials);
    bms::service::AccountUnlockService unlockService(credentials);
    bms::service::AuthService authService(credentials);
    bms::service::SystemStatusService systemStatusService(
        credentials,
        customerData,
        "data/credentials.dat",
        "data/customers.dat",
        "data/accounts.dat"
    );
    bms::service::SessionManager session;

    while (true) {
        std::cout << "\nBank Management System\n"
                  << "Customer features after login: balance, deposit, withdraw, "
                     "account details, change PIN\n"
                  << "Admin features after login: create account, unlock login, "
                     "system status, change PIN\n"
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
                session.login(userId, bms::infra::Role::CUSTOMER, 300);
                customerMenu(
                    userId,
                    accounts,
                    transactionService,
                    customerData,
                    authService
                );
                session.logout();
            }
        } else if (selection == "2") {
            if (login(
                    loginService,
                    bms::infra::Role::ADMIN,
                    userId)) {
                adminMenu(
                    userId,
                    session,
                    accountService,
                    unlockService,
                    systemStatusService,
                    authService,
                    credentials,
                    customerData,
                    accounts
                );
            }
        } else {
            std::cout << "Invalid selection.\n";
        }
    }
}