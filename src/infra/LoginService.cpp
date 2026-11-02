#include "infra/LoginService.h"

#include "infra/PasswordHasher.h"

#include <string>

namespace bms::infra {

LoginResult LoginService::authenticate(const std::string& userId,
                                      const std::string& password,
                                      Role expectedRole) {
    Credential* credential = store_.find(userId);
    if (credential == nullptr) {
        if (auditLogger_) auditLogger_->log("LOGIN", userId, "INVALID_CREDENTIALS", "reason=UNKNOWN_USER");
        return {LoginStatus::INVALID_CREDENTIALS, 0};
    }
    if (credential->locked) {
        if (auditLogger_) auditLogger_->log("LOGIN", userId, "ACCOUNT_LOCKED");
        return {LoginStatus::ACCOUNT_LOCKED, 0};
    }

    if (credential->role != expectedRole ||
        !PasswordHasher::verify(password, credential->salt, credential->hashHex)) {
        ++credential->failedAttempts;
        if (credential->failedAttempts >= kMaxFailedAttempts) {
            credential->locked = true;
        }
        if (!store_.save()) {
            if (auditLogger_) auditLogger_->log("LOGIN", userId, "STORAGE_ERROR", "reason=FAILED_ATTEMPT_SAVE");
            return {LoginStatus::STORAGE_ERROR, 0};
        }
        if (credential->locked) {
            if (auditLogger_) auditLogger_->log("LOGIN", userId, "ACCOUNT_LOCKED", "reason=FAILED_ATTEMPTS");
            return {LoginStatus::ACCOUNT_LOCKED, 0};
        }
        if (auditLogger_) auditLogger_->log("LOGIN", userId, "INVALID_CREDENTIALS", "attemptsRemaining=" + std::to_string(kMaxFailedAttempts - credential->failedAttempts));
        return {LoginStatus::INVALID_CREDENTIALS,
                kMaxFailedAttempts - credential->failedAttempts};
    }

    credential->failedAttempts = 0;
    if (!store_.save()) {
        if (auditLogger_) auditLogger_->log("LOGIN", userId, "STORAGE_ERROR", "reason=SUCCESS_STATE_SAVE");
        return {LoginStatus::STORAGE_ERROR, 0};
    }
    if (auditLogger_) auditLogger_->log("LOGIN", userId, "SUCCESS");
    return {LoginStatus::SUCCESS, kMaxFailedAttempts};
}

} // namespace bms::infra