#include "service/AuthService.h"
#include "infra/CredentialStore.h"
#include "infra/PasswordHasher.h"
#include "TestHarness.h"
#include "TestUtil.h"

#include <string>

using namespace bms::service;
using namespace bms::infra;
using namespace bms::domain;

static void test_change_credentials() {
    bms::test::TempDir dir;
    const std::string testFile = dir.file("credentials.dat");
    CredentialStore store(testFile);
    Credential c;
    c.userId = "testuser";
    c.role = Role::CUSTOMER;
    c.salt = PasswordHasher::generateSalt();
    c.hashHex = PasswordHasher::hash("1234", c.salt);
    store.upsert(c);
    CHECK(store.save());

    AuthService auth(store);

    CHECK(auth.changeCredentials("testuser", "9999", "5678") == Status::UNAUTHORIZED);

    CHECK(auth.changeCredentials("testuser", "1234", "12") == Status::INVALID_INPUT);
    CHECK(auth.changeCredentials("testuser", "1234", "12AB") == Status::INVALID_INPUT);

    CHECK(auth.changeCredentials("testuser", "1234", "5678") == Status::OK);

    CredentialStore reloaded(testFile);
    CHECK(reloaded.load());
    Credential* updated = reloaded.find("testuser");
    CHECK(updated != nullptr);
    if (updated != nullptr) {
        CHECK(PasswordHasher::verify("5678", updated->salt, updated->hashHex));
        CHECK(!PasswordHasher::verify("1234", updated->salt, updated->hashHex));
    }
}

int main() {
    test_change_credentials();
    return bms::test::summary("test_change_credentials");
}