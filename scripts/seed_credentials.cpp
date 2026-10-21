#include <iostream>

#include "infra/CredentialStore.h"
#include "infra/PasswordHasher.h"
#include "infra/FileUtil.h"

using namespace bms::infra;

static Credential make(const std::string& id, Role role, const std::string& pin) {
    Credential c;
    c.userId = id;
    c.role = role;
    c.salt = PasswordHasher::generateSalt();
    c.hashHex = PasswordHasher::hash(pin, c.salt);
    c.failedAttempts = 0;
    c.locked = false;
    return c;
}

int main() {
    ensureDir("data");
    CredentialStore store("data/credentials.dat");
    store.upsert(make("admin",    Role::ADMIN,    "admin123"));
    store.upsert(make("cust1001", Role::CUSTOMER, "1234"));
    store.upsert(make("cust1002", Role::CUSTOMER, "5678"));
    if (!store.save()) {
        std::cerr << "Failed to write data/credentials.dat\n";
        return 1;
    }
    std::cout << "Wrote data/credentials.dat with 3 seed users.\n";
    return 0;
}