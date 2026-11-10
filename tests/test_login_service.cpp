#include "infra/CredentialStore.h"
#include "infra/FileUtil.h"
#include "infra/LoginService.h"
#include "infra/PasswordHasher.h"
#include "service/RbacGuard.h"
#include "service/SessionManager.h"
#include "TestHarness.h"
#include "TestUtil.h"

#include <filesystem>
#include <string>

using namespace bms::infra;

namespace {

Credential makeCredential(const std::string& userId, Role role,
                          const std::string& password) {
    Credential credential;
    credential.userId = userId;
    credential.role = role;
    credential.salt = PasswordHasher::generateSalt();
    credential.hashHex = PasswordHasher::hash(password, credential.salt);
    return credential;
}

void test_customer_and_admin_login() {
    const std::string path = "test_login_credentials.dat";
    CredentialStore store(path);
    store.upsert(makeCredential("customer", Role::CUSTOMER, "1234"));
    store.upsert(makeCredential("admin", Role::ADMIN, "admin123"));
    CHECK(store.save());

    CredentialStore loaded(path);
    CHECK(loaded.load());
    LoginService service(loaded);
    CHECK(service.authenticate("customer", "1234", Role::CUSTOMER).status ==
          LoginStatus::SUCCESS);
    CHECK(service.authenticate("admin", "admin123", Role::ADMIN).status ==
          LoginStatus::SUCCESS);
    std::filesystem::remove(path);
}

void test_invalid_attempts_lock_and_reject_login() {
    const std::string path = "test_login_lockout.dat";
    CredentialStore store(path);
    store.upsert(makeCredential("customer", Role::CUSTOMER, "1234"));
    CHECK(store.save());

    LoginService service(store);
    const auto first = service.authenticate("customer", "wrong", Role::CUSTOMER);
    CHECK(first.status == LoginStatus::INVALID_CREDENTIALS);
    CHECK(first.attemptsRemaining == 2);
    const auto second = service.authenticate("customer", "wrong", Role::CUSTOMER);
    CHECK(second.status == LoginStatus::INVALID_CREDENTIALS);
    CHECK(second.attemptsRemaining == 1);
    const auto third = service.authenticate("customer", "1234", Role::ADMIN);
    CHECK(third.status == LoginStatus::ACCOUNT_LOCKED);

    CredentialStore reloaded(path);
    CHECK(reloaded.load());
    const Credential* persisted = reloaded.find("customer");
    CHECK(persisted != nullptr);
    if (persisted != nullptr) {
        CHECK(persisted->locked);
        CHECK(persisted->failedAttempts == LoginService::kMaxFailedAttempts);
    }
    CHECK(service.authenticate("customer", "1234", Role::CUSTOMER).status ==
          LoginStatus::ACCOUNT_LOCKED);
    std::filesystem::remove(path);
}

void test_success_resets_failed_attempts() {
    const std::string path = "test_login_reset.dat";
    CredentialStore store(path);
    Credential credential = makeCredential("customer", Role::CUSTOMER, "1234");
    credential.failedAttempts = 1;
    store.upsert(credential);
    CHECK(store.save());

    LoginService service(store);
    CHECK(service.authenticate("customer", "1234", Role::CUSTOMER).status ==
          LoginStatus::SUCCESS);
    CHECK(store.find("customer")->failedAttempts == 0);

    CredentialStore reloaded(path);
    CHECK(reloaded.load());
    CHECK(reloaded.find("customer")->failedAttempts == 0);
    std::filesystem::remove(path);
}

void test_unknown_user_does_not_disclose_attempts() {
    const std::string path = "test_login_unknown.dat";
    CredentialStore store(path);
    LoginService service(store);
    const auto result = service.authenticate("missing", "guess", Role::CUSTOMER);
    CHECK(result.status == LoginStatus::INVALID_CREDENTIALS);
    CHECK(result.attemptsRemaining == 0);
    std::filesystem::remove(path);
}

void test_authenticated_sessions_enforce_role_access() {
    bms::test::TempDir dir;
    CredentialStore store(dir.file("credentials.dat"));
    store.upsert(makeCredential("customer", Role::CUSTOMER, "1234"));
    store.upsert(makeCredential("admin", Role::ADMIN, "admin123"));
    CHECK(store.save());

    CredentialStore reloaded(dir.file("credentials.dat"));
    CHECK(reloaded.load());
    LoginService login(reloaded);
    bms::service::SessionManager session;

    CHECK(
        bms::service::RbacGuard::checkRole(session, Role::CUSTOMER)
        == bms::domain::Status::UNAUTHORIZED
    );

    const auto rejected =
        login.authenticate("customer", "wrong", Role::CUSTOMER);
    CHECK(rejected.status == LoginStatus::INVALID_CREDENTIALS);
    CHECK(!session.isActive());

    const auto customerLogin =
        login.authenticate("customer", "1234", Role::CUSTOMER);
    CHECK(customerLogin.status == LoginStatus::SUCCESS);
    if (customerLogin.status == LoginStatus::SUCCESS) {
        session.login("customer", Role::CUSTOMER, 300);
    }
    CHECK(
        bms::service::RbacGuard::checkRole(session, Role::CUSTOMER)
        == bms::domain::Status::OK
    );
    CHECK(
        bms::service::RbacGuard::checkRole(session, Role::ADMIN)
        == bms::domain::Status::UNAUTHORIZED
    );

    session.logout();
    const auto adminLogin =
        login.authenticate("admin", "admin123", Role::ADMIN);
    CHECK(adminLogin.status == LoginStatus::SUCCESS);
    if (adminLogin.status == LoginStatus::SUCCESS) {
        session.login("admin", Role::ADMIN, 300);
    }
    CHECK(
        bms::service::RbacGuard::checkRole(session, Role::ADMIN)
        == bms::domain::Status::OK
    );
    CHECK(
        bms::service::RbacGuard::checkRole(session, Role::CUSTOMER)
        == bms::domain::Status::UNAUTHORIZED
    );

    session.logout();
    CHECK(
        bms::service::RbacGuard::checkRole(session, Role::ADMIN)
        == bms::domain::Status::UNAUTHORIZED
    );
}

void test_login_attempts_are_audited_without_credentials() {
    bms::test::TempDir dir;
    const std::string credentialPath = dir.file("credentials.dat");
    const std::string auditPath = dir.file("audit.log");
    CredentialStore store(credentialPath);
    store.upsert(makeCredential("audited-user", Role::CUSTOMER, "secret-pin"));
    CHECK(store.save());

    AuditLogger audit(auditPath);
    LoginService service(store, &audit);
    CHECK(service.authenticate("audited-user", "wrong-pin", Role::CUSTOMER).status
          == LoginStatus::INVALID_CREDENTIALS);
    CHECK(service.authenticate("audited-user", "secret-pin", Role::CUSTOMER).status
          == LoginStatus::SUCCESS);

    const std::string log = bms::test::readFile(auditPath);
    CHECK(log.find("|LOGIN|audited-user|INVALID_CREDENTIALS|") != std::string::npos);
    CHECK(log.find("|LOGIN|audited-user|SUCCESS|") != std::string::npos);
    CHECK(log.find("wrong-pin") == std::string::npos);
    CHECK(log.find("secret-pin") == std::string::npos);
}

void test_storage_errors_are_reported_for_failed_and_successful_login() {
    bms::test::TempDir dir;
    const std::string credentialPath = dir.file("credentials.dat");
    CHECK(std::filesystem::create_directory(credentialPath));

    CredentialStore store(credentialPath);
    store.upsert(makeCredential("storage-user", Role::CUSTOMER, "1234"));

    AuditLogger audit(dir.file("audit.log"));
    LoginService service(store, &audit);
    CHECK(service.authenticate("storage-user", "wrong", Role::CUSTOMER).status
          == LoginStatus::STORAGE_ERROR);
    CHECK(service.authenticate("storage-user", "1234", Role::CUSTOMER).status
          == LoginStatus::STORAGE_ERROR);

    const std::string log = bms::test::readFile(dir.file("audit.log"));
    CHECK(log.find("|LOGIN|storage-user|STORAGE_ERROR|reason=FAILED_ATTEMPT_SAVE")
          != std::string::npos);
    CHECK(log.find("|LOGIN|storage-user|STORAGE_ERROR|reason=SUCCESS_STATE_SAVE")
          != std::string::npos);
}

} // namespace

int main() {
    test_customer_and_admin_login();
    test_invalid_attempts_lock_and_reject_login();
    test_success_resets_failed_attempts();
    test_unknown_user_does_not_disclose_attempts();
    test_authenticated_sessions_enforce_role_access();
    test_login_attempts_are_audited_without_credentials();
    test_storage_errors_are_reported_for_failed_and_successful_login();
    return bms::test::summary("test_login_service");
}