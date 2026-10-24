#pragma once

#include <string>

#include "domain/Status.h"
#include "infra/CredentialStore.h"
#include "service/SessionManager.h"

namespace bms::service {

class AccountUnlockService {
public:
    explicit AccountUnlockService(infra::CredentialStore& store);

    domain::Status unlockAccount(const SessionManager& session,
                                 const std::string& targetUserId);

private:
    infra::CredentialStore& store_;
};

} // namespace bms::service
