#include "TestHarness.h"
#include "TestUtil.h"

#include "domain/Status.h"
#include "infra/AccountRepository.h"
#include "infra/AuditLogger.h"
#include "infra/Config.h"
#include "infra/CredentialStore.h"
#include "infra/CustomerAccountStore.h"
#include "infra/LoginService.h"
#include "infra/TransactionRepository.h"
#include "service/AccountService.h"
#include "service/RbacGuard.h"
#include "service/SessionManager.h"
#include "service/TransactionService.h"

#include <string>

namespace {

void test_customer_banking_workflow() {
    bms::test::TempDir dir;
    const std::string customersPath = dir.file("customers.dat");
    const std::string accountsPath = dir.file("accounts.dat");
    const std::string credentialsPath = dir.file("credentials.dat");
    const std::string transactionsPath = dir.file("transactions.dat");
    const std::string auditPath = dir.file("audit.log");

    bms::infra::CustomerAccountStore customerStore(
        customersPath, accountsPath
    );
    bms::infra::CredentialStore credentials(credentialsPath);
    bms::infra::AuditLogger audit(auditPath);
    bms::service::AccountService accountService(
        customerStore, credentials, &audit
    );

    std::string sourceAccount;
    std::string destinationAccount;
    CHECK(accountService.createAccount(
        {"Source Customer", "9876543210", "Bengaluru", "1234"},
        sourceAccount
    ) == bms::domain::Status::OK);
    CHECK(accountService.createAccount(
        {"Destination Customer", "9876543211", "Mysuru", "5678"},
        destinationAccount
    ) == bms::domain::Status::OK);
    CHECK(!sourceAccount.empty());
    CHECK(!destinationAccount.empty());
    CHECK(sourceAccount != destinationAccount);

    CHECK(credentials.load());
    bms::infra::LoginService login(credentials, &audit);
    const auto loginResult = login.authenticate(
        "CUST0001", "1234", bms::infra::Role::CUSTOMER
    );
    CHECK(loginResult.status == bms::infra::LoginStatus::SUCCESS);

    bms::service::SessionManager session;
    if (loginResult.status == bms::infra::LoginStatus::SUCCESS) {
        session.login("CUST0001", bms::infra::Role::CUSTOMER, 300);
    }
    CHECK(session.isActive());
    CHECK(bms::service::RbacGuard::checkRole(
        session, bms::infra::Role::CUSTOMER
    ) == bms::domain::Status::OK);

    bms::infra::AccountRepository accounts(accountsPath);
    bms::infra::TransactionRepository transactions(transactionsPath);
    CHECK(accounts.load().isOk());
    CHECK(transactions.all().empty());

    bms::infra::Config config;
    bms::service::TransactionService banking(
        accounts, transactions, config, audit
    );

    CHECK(banking.deposit(
        sourceAccount, 20000, bms::service::DepositMode::CASH
    ).status == bms::service::TxnStatus::OK);
    CHECK(banking.transfer(
        sourceAccount, destinationAccount, 7000
    ).status == bms::service::TxnStatus::OK);
    CHECK(banking.withdraw(destinationAccount, 2000).status
          == bms::service::TxnStatus::OK);
    CHECK(banking.transfer(
        sourceAccount, destinationAccount, 0
    ).status == bms::service::TxnStatus::INVALID_INPUT);

    CHECK(banking.getBalance(sourceAccount).newBalancePaise == 13000);
    CHECK(banking.getBalance(destinationAccount).newBalancePaise == 5000);

    bms::infra::AccountRepository persistedAccounts(accountsPath);
    bms::infra::TransactionRepository persistedTransactions(
        transactionsPath
    );
    CHECK(persistedAccounts.load().isOk());
    CHECK(persistedTransactions.load().isOk());
    const auto* persistedSource = persistedAccounts.find(sourceAccount);
    const auto* persistedDestination =
        persistedAccounts.find(destinationAccount);
    CHECK(persistedSource != nullptr);
    CHECK(persistedDestination != nullptr);
    if (persistedSource != nullptr) {
        CHECK(persistedSource->balancePaise == 13000);
    }
    if (persistedDestination != nullptr) {
        CHECK(persistedDestination->balancePaise == 5000);
    }
    CHECK(persistedTransactions.pending().empty());

    const std::string auditLog = bms::test::readFile(auditPath);
    CHECK(auditLog.find("|CUSTOMER_ACCOUNT_CREATED|CUST0001|SUCCESS|")
          != std::string::npos);
    CHECK(auditLog.find("|LOGIN|CUST0001|SUCCESS|")
          != std::string::npos);
    CHECK(auditLog.find("|DEPOSIT|" + sourceAccount + "|SUCCESS|")
          != std::string::npos);
    CHECK(auditLog.find("|WITHDRAWAL|" + destinationAccount + "|SUCCESS|")
          != std::string::npos);

    session.logout();
    CHECK(!session.isActive());
}

} // namespace

int main() {
    test_customer_banking_workflow();
    return bms::test::summary("test_system_validation");
}
