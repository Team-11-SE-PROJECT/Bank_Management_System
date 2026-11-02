#include "infra/CredentialStore.h"
#include "infra/FileUtil.h"
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

void test_save_load_roundtrip_and_upsert() {
    const std::string path = "test_credential_store.dat";
    CredentialStore store(path);
    Credential customerCredential =
        makeCredential("customer", Role::CUSTOMER, "1234");
    customerCredential.failedAttempts = 3;
    customerCredential.locked = true;
    store.upsert(customerCredential);
    store.upsert(makeCredential("admin", Role::ADMIN, "admin123"));
    Credential updatedCustomer =
        makeCredential("customer", Role::CUSTOMER, "5678");
    updatedCustomer.failedAttempts = 3;
    updatedCustomer.locked = true;
    store.upsert(updatedCustomer);
    CHECK_EQ(store.all().size(), std::size_t(2));
    CHECK(store.save());

    CredentialStore loaded(path);
    CHECK(loaded.load());
    CHECK_EQ(loaded.all().size(), std::size_t(2));
    const Credential* customer = loaded.find("customer");
    const Credential* admin = loaded.find("admin");
    CHECK(customer != nullptr);
    CHECK(admin != nullptr);
    if (customer != nullptr) {
        CHECK(customer->role == Role::CUSTOMER);
        CHECK(PasswordHasher::verify("5678", customer->salt, customer->hashHex));
        CHECK_EQ(customer->failedAttempts, 3);
        CHECK(customer->locked);
    }
    if (admin != nullptr) {
        CHECK(admin->role == Role::ADMIN);
        CHECK(PasswordHasher::verify("admin123", admin->salt, admin->hashHex));
    }
    std::filesystem::remove(path);
}

void test_role_conversion() {
    Role role = Role::CUSTOMER;
    CHECK_EQ(std::string(CredentialStore::roleToString(Role::ADMIN)),
             std::string("ADMIN"));
    CHECK(CredentialStore::roleFromString("CUSTOMER", role));
    CHECK(role == Role::CUSTOMER);
    CHECK(!CredentialStore::roleFromString("UNKNOWN", role));
}

void test_missing_and_malformed_files() {
    const std::string missingPath = "test_credential_store_missing.dat";
    std::filesystem::remove(missingPath);
    CredentialStore missing(missingPath);
    CHECK(missing.load());
    CHECK(missing.all().empty());

    const std::string malformedPath = "test_credential_store_malformed.dat";
    CHECK(atomicWrite(malformedPath, "user|INVALID_ROLE|salt|hash|0|0\n"));
    CredentialStore malformed(malformedPath);
    CHECK(!malformed.load());
    std::filesystem::remove(malformedPath);
}

} // namespace

int main() {
    test_save_load_roundtrip_and_upsert();
    test_role_conversion();
    test_missing_and_malformed_files();
    return bms::test::summary("test_credential_store");
}