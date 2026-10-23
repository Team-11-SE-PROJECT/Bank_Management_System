#include "service/AuthService.h"
#include "infra/CredentialStore.h"
#include "infra/PasswordHasher.h"
#include "infra/FileUtil.h"
#include "TestHarness.h"
#include <filesystem>
#include <iostream>
using namespace bms::service;
using namespace bms::infra;
using namespace bms::domain;

static void test_change_credentials() {
    // 1. Setup a dummy store
    const std::string testFile = "test_creds.dat";
    std::filesystem::remove(testFile);
    
    CredentialStore store(testFile);
    Credential c;
    c.userId = "testuser";
    c.role = Role::CUSTOMER;
    c.salt = PasswordHasher::generateSalt();
    c.hashHex = PasswordHasher::hash("1234", c.salt); // Old PIN is 1234
    store.upsert(c);
    store.save();

    AuthService auth(store);

    // 2. Test wrong old PIN
    CHECK(auth.changeCredentials("testuser", "9999", "5678") == Status::UNAUTHORIZED);

    // 3. Test invalid new PIN (too short)
    CHECK(auth.changeCredentials("testuser", "1234", "12") == Status::INVALID_INPUT);

    // 4. Test successful change
    CHECK(auth.changeCredentials("testuser", "1234", "5678") == Status::OK);

    // 5. Verify the new PIN is saved correctly
    CredentialStore reloaded(testFile);
    reloaded.load();
    Credential* updated = reloaded.find("testuser");
    CHECK(updated != nullptr);
    CHECK(PasswordHasher::verify("5678", updated->salt, updated->hashHex));
    CHECK(!PasswordHasher::verify("1234", updated->salt, updated->hashHex)); // Old one should fail

    std::filesystem::remove(testFile);
}

int main() {
    test_change_credentials();
    return bms::test::summary("test_change_credentials");
}