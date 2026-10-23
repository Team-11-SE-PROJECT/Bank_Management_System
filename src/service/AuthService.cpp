#include "service/AuthService.h"
#include "infra/PasswordHasher.h"
using namespace bms::domain;
using namespace bms::infra;
namespace bms::service {
AuthService::AuthService(CredentialStore& store) : store_(store) {}
Status AuthService::changeCredentials(const std::string& userId, 
                                      const std::string& oldPin, 
                                      const std::string& newPin) {
    // 1. Find the user
    Credential* cred = store_.find(userId);
    if (!cred) {
        return Status::NOT_FOUND;
    }

    // 2. Verify the old PIN
    if (!PasswordHasher::verify(oldPin, cred->salt, cred->hashHex)) {
        return Status::UNAUTHORIZED; // Old PIN wrong
    }

    // 3. Validate the new PIN (4-6 digits, based on SAD/config)
    if (newPin.length() < 4 || newPin.length() > 6) {
        return Status::INVALID_INPUT; 
    }

    // 4. Hash the new PIN with a fresh salt
    cred->salt = PasswordHasher::generateSalt();
    cred->hashHex = PasswordHasher::hash(newPin, cred->salt);

    // 5. Save to file atomically
    if (!store_.save()) {
        return Status::STORAGE_FAILURE;
    }

    return Status::OK;
}

// TODO: Ianna will implement login() here for Issue #10

} // namespace bms::service