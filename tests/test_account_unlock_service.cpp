#include "service/AccountUnlockService.h"
#include "infra/PasswordHasher.h"
#include "TestHarness.h"
#include "TestUtil.h"

#include <string>

using namespace bms::domain;
using namespace bms::infra;
using namespace bms::service;

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

void test_admin_can_unlock_account() {
    bms::test::TempDir dir;
    const std::string path = dir.file("credentials.dat");
    CredentialStore store(path);

    Credential customer = makeCredential("customer", Role::CUSTOMER, "1234");
    customer.failedAttempts = 3;
    customer.locked = true;
    store.upsert(customer);
    CHECK(store.save());

    SessionManager session;
    session.login("admin", Role::ADMIN, 300);

    AccountUnlockService service(store);
    CHECK(service.unlockAccount(session, "customer") == Status::OK);

    const Credential* unlocked = store.find("customer");
    CHECK(unlocked != nullptr);
    if (unlocked != nullptr) {
        CHECK(!unlocked->locked);
        CHECK(unlocked->failedAttempts == 0);
    }

    CredentialStore reloaded(path);
    CHECK(reloaded.load());
    const Credential* persisted = reloaded.find("customer");
    CHECK(persisted != nullptr);
    if (persisted != nullptr) {
        CHECK(!persisted->locked);
        CHECK(persisted->failedAttempts == 0);
    }
}

void test_customer_cannot_unlock_account() {
    bms::test::TempDir dir;
    const std::string path = dir.file("credentials.dat");
    CredentialStore store(path);

    Credential customer = makeCredential("customer", Role::CUSTOMER, "1234");
    customer.failedAttempts = 3;
    customer.locked = true;
    store.upsert(customer);
    CHECK(store.save());

    SessionManager session;
    session.login("customer", Role::CUSTOMER, 300);

    AccountUnlockService service(store);
    CHECK(service.unlockAccount(session, "customer") == Status::UNAUTHORIZED);

    const Credential* saved = store.find("customer");
    CHECK(saved != nullptr);
    if (saved != nullptr) {
        CHECK(saved->locked);
        CHECK(saved->failedAttempts == 3);
    }

}

void test_inactive_session_cannot_unlock_account() {
    bms::test::TempDir dir;
    const std::string path = dir.file("credentials.dat");
    CredentialStore store(path);

    Credential customer = makeCredential("customer", Role::CUSTOMER, "1234");
    customer.locked = true;
    customer.failedAttempts = 3;
    store.upsert(customer);
    CHECK(store.save());

    SessionManager session;
    AccountUnlockService service(store);
    CHECK(service.unlockAccount(session, "customer") == Status::UNAUTHORIZED);

    CHECK(store.find("customer")->locked);
}

void test_missing_account_returns_not_found() {
    bms::test::TempDir dir;
    const std::string path = dir.file("credentials.dat");
    CredentialStore store(path);
    CHECK(store.save());

    SessionManager session;
    session.login("admin", Role::ADMIN, 300);

    AccountUnlockService service(store);
    CHECK(service.unlockAccount(session, "missing") == Status::NOT_FOUND);

}

} // namespace

int main() {
    test_admin_can_unlock_account();
    test_customer_cannot_unlock_account();
    test_inactive_session_cannot_unlock_account();
    test_missing_account_returns_not_found();
    return bms::test::summary("test_account_unlock_service");
}
