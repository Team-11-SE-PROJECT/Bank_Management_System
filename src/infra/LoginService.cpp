#include "infra/LoginService.h"

#include "infra/PasswordHasher.h"

namespace bms::infra {

LoginResult LoginService::authenticate(const std::string& userId,
                                      const std::string& password,
                                      Role expectedRole) {
    Credential* credential = store_.find(userId);
    if (credential == nullptr) {
        return {LoginStatus::INVALID_CREDENTIALS, 0};
    }
    if (credential->locked) {
        return {LoginStatus::ACCOUNT_LOCKED, 0};
    }

    if (credential->role != expectedRole ||
        !PasswordHasher::verify(password, credential->salt, credential->hashHex)) {
        ++credential->failedAttempts;
        if (credential->failedAttempts >= kMaxFailedAttempts) {
            credential->locked = true;
        }
        if (!store_.save()) {
            return {LoginStatus::STORAGE_ERROR, 0};
        }
        if (credential->locked) {
            return {LoginStatus::ACCOUNT_LOCKED, 0};
        }
        return {LoginStatus::INVALID_CREDENTIALS,
                kMaxFailedAttempts - credential->failedAttempts};
    }

    credential->failedAttempts = 0;
    if (!store_.save()) {
        return {LoginStatus::STORAGE_ERROR, 0};
    }
    return {LoginStatus::SUCCESS, kMaxFailedAttempts};
}

} // namespace bms::infra