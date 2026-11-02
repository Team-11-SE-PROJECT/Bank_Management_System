#include <filesystem>
#include <iostream>

#include "infra/AccountRepository.h"
#include "infra/AuditLogger.h"
#include "infra/Config.h"
#include "infra/CredentialStore.h"
#include "infra/CustomerAccountStore.h"
#include "infra/Journal.h"
#include "infra/LoadStatus.h"
#include "infra/LoginService.h"
#include "infra/TransactionRepository.h"
#include "service/AccountService.h"
#include "service/AccountClosureService.h"
#include "service/AccountUnlockService.h"
#include "service/AuditLogReviewService.h"
#include "service/AuthService.h"
#include "service/CustomerSearchService.h"
#include "service/CustomerUpdateService.h"
#include "service/LocalWebServer.h"
#include "service/SystemStatusService.h"
#include "service/TransactionService.h"

int main(int argc, char* argv[]) {
    unsigned long port = 8080;
    if (argc > 2) {
        std::cerr << "Usage: bms_web [port]\n";
        return 2;
    }
    if (argc == 2) {
        try {
            std::size_t parsed = 0;
            port = std::stoul(argv[1], &parsed);
            if (parsed != std::string(argv[1]).size() ||
                port == 0 || port > 65535) {
                throw std::invalid_argument("port out of range");
            }
        } catch (const std::exception&) {
            std::cerr << "Port must be a number from 1 to 65535.\n";
            return 2;
        }
    }

    std::error_code ec;
    std::filesystem::create_directories("data", ec);
    if (ec) {
        std::cerr << "Cannot create data directory: " << ec.message() << '\n';
        return 1;
    }

    bms::infra::Config config;
    if (config.load("config.ini") == bms::infra::LoadStatus::CORRUPT) {
        std::cerr << "Invalid config.ini: " << config.lastError() << '\n';
        return 1;
    }

    bms::infra::CredentialStore credentials("data/credentials.dat");
    if (!credentials.load()) {
        std::cerr << "Could not load credential data.\n";
        return 1;
    }

    bms::infra::AccountRepository accounts("data/accounts.dat");
    const auto accountStatus = accounts.load();
    if (accountStatus == bms::infra::LoadStatus::CORRUPT) {
        std::cerr << "Error loading data/accounts.dat: "
                  << accounts.lastError() << '\n';
        return 1;
    }

    bms::infra::TransactionRepository transactions("data/transactions.dat");
    const auto transactionStatus = transactions.load();
    if (transactionStatus == bms::infra::LoadStatus::CORRUPT) {
        std::cerr << "Error loading data/transactions.dat: "
                  << transactions.lastError() << '\n';
        return 1;
    }

    bms::infra::CustomerAccountStore customerData(
        "data/customers.dat", "data/accounts.dat"
    );
    const auto customerStatus = customerData.loadWithStatus();
    if (customerStatus == bms::infra::LoadStatus::CORRUPT) {
        std::cerr << "Error loading customer data: "
                  << customerStatus.errorMessage << '\n';
        return 1;
    }

    bms::infra::AuditLogger audit("data/audit.log");
    bms::service::CustomerUpdateService customerUpdateService(customerData);
    bms::service::CustomerSearchService customerSearchService(customerData);
    bms::service::AccountClosureService accountClosureService(accounts, &audit);
    bms::service::AuditLogReviewService auditLogReviewService("data/audit.log");
    bms::infra::Journal journal("data/journal.dat");
    const auto journalStatus = journal.load();
    if (journalStatus == bms::infra::LoadStatus::CORRUPT) {
        std::cerr << "Error loading data/journal.dat: "
                  << journal.lastError() << '\n';
        return 1;
    }
    const int recovered = bms::infra::Journal::recover(
        journal, accounts, transactions, &audit
    );
    if (recovered > 0) {
        std::cout << "Crash recovery complete: " << recovered
                  << " journal entries reconciled.\n";
    }

    bms::infra::LoginService login(credentials, &audit);
    bms::service::TransactionService transactionService(
        accounts, transactions, config, audit
    );
    transactionService.setJournal(&journal);
    bms::service::AccountService accountService(customerData, credentials, &audit);
    bms::service::AccountUnlockService unlockService(credentials, &audit);
    bms::service::AuthService authService(credentials, &audit);
    bms::service::SystemStatusService systemStatusService(
        credentials,
        customerData,
        "data/credentials.dat",
        "data/customers.dat",
        "data/accounts.dat"
    );

    bms::service::LocalWebServer server(
        login, credentials, accounts, transactions, customerData,
        transactionService, accountService, customerUpdateService,
        unlockService, customerSearchService, accountClosureService,
        auditLogReviewService,
        systemStatusService, authService
    );

    std::cout << "Bank Management System web UI: http://127.0.0.1:"
              << port << "/\n"
              << "Listening on localhost only. Press Ctrl+C to stop.\n";
    return server.serve(static_cast<unsigned short>(port)) ? 0 : 1;
}
