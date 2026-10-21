#include "infra/CredentialStore.h"
#include "infra/FileUtil.h"
#include "infra/LoginService.h"
#include "infra/PasswordHasher.h"
#include "TestHarness.h"

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

} // namespace

int main() {
    test_customer_and_admin_login();
    test_invalid_attempts_lock_and_reject_login();
    test_success_resets_failed_attempts();
    test_unknown_user_does_not_disclose_attempts();
    return bms::test::summary("test_login_service");
}