#include "service/SystemStatusService.h"
#include "infra/PasswordHasher.h"
#include "TestHarness.h"

#include <filesystem>
#include <string>

using namespace bms::domain;
using namespace bms::infra;
using namespace bms::service;

namespace {

void test_admin_can_read_status() {
    const std::string credentialsPath = "test_status_credentials.dat";
    const std::string customersPath = "test_status_customers.dat";
    const std::string accountsPath = "test_status_accounts.dat";

    CredentialStore credentials(credentialsPath);
    Credential credential;
    credential.userId = "customer";
    credential.role = Role::CUSTOMER;
    credential.salt = PasswordHasher::generateSalt();
    credential.hashHex = PasswordHasher::hash("1234", credential.salt);
    credentials.upsert(credential);
    CHECK(credentials.save());

    CustomerAccountStore customers(customersPath, accountsPath);
    CHECK(customers.load());

    SessionManager session;
    session.login("admin", Role::ADMIN, 300);

    SystemStatusService service(
        credentials, customers, credentialsPath, customersPath, accountsPath
    );

    SystemStatus status;
    CHECK(service.getStatus(session, status) == Status::OK);
    CHECK(status.applicationRunning);
    CHECK(status.credentialsFileAccessible);
    CHECK(status.credentialCount == 1);

    std::filesystem::remove(credentialsPath);
    std::filesystem::remove(customersPath);
    std::filesystem::remove(accountsPath);
}

void test_customer_cannot_read_status() {
    const std::string credentialsPath = "test_status_customer_credentials.dat";
    const std::string customersPath = "test_status_customer_customers.dat";
    const std::string accountsPath = "test_status_customer_accounts.dat";

    CredentialStore credentials(credentialsPath);
    CustomerAccountStore customers(customersPath, accountsPath);

    SessionManager session;
    session.login("customer", Role::CUSTOMER, 300);

    SystemStatusService service(
        credentials, customers, credentialsPath, customersPath, accountsPath
    );

    SystemStatus status;
    CHECK(service.getStatus(session, status) == Status::UNAUTHORIZED);

    std::filesystem::remove(credentialsPath);
    std::filesystem::remove(customersPath);
    std::filesystem::remove(accountsPath);
}

void test_inactive_session_cannot_read_status() {
    const std::string credentialsPath = "test_status_inactive_credentials.dat";
    const std::string customersPath = "test_status_inactive_customers.dat";
    const std::string accountsPath = "test_status_inactive_accounts.dat";

    CredentialStore credentials(credentialsPath);
    CustomerAccountStore customers(customersPath, accountsPath);

    SessionManager session;
    SystemStatusService service(
        credentials, customers, credentialsPath, customersPath, accountsPath
    );

    SystemStatus status;
    CHECK(service.getStatus(session, status) == Status::UNAUTHORIZED);

    std::filesystem::remove(credentialsPath);
    std::filesystem::remove(customersPath);
    std::filesystem::remove(accountsPath);
}

} // namespace

int main() {
    test_admin_can_read_status();
    test_customer_cannot_read_status();
    test_inactive_session_cannot_read_status();
    return bms::test::summary("test_system_status_service");
}
