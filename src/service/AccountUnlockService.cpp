#include "service/AccountUnlockService.h"

#include "service/RbacGuard.h"

namespace bms::service {

AccountUnlockService::AccountUnlockService(infra::CredentialStore& store)
    : store_(store) {}

domain::Status AccountUnlockService::unlockAccount(
    const SessionManager& session,
    const std::string& targetUserId) {

    const domain::Status access =
        RbacGuard::checkRole(session, infra::Role::ADMIN);

    if (access != domain::Status::OK) {
        return domain::Status::UNAUTHORIZED;
    }

    if (targetUserId.empty()) {
        return domain::Status::INVALID_INPUT;
    }

    infra::Credential* credential = store_.find(targetUserId);

    if (credential == nullptr) {
        return domain::Status::NOT_FOUND;
    }

    const bool wasLocked = credential->locked;
    const int previousAttempts = credential->failedAttempts;

    credential->locked = false;
    credential->failedAttempts = 0;

    if (!store_.save()) {
        credential->locked = wasLocked;
        credential->failedAttempts = previousAttempts;
        return domain::Status::STORAGE_FAILURE;
    }

    return domain::Status::OK;
}

} // namespace bms::service
