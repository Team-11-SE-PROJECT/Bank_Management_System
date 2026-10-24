#pragma once

#include <string>

#include "domain/Status.h"
#include "infra/CredentialStore.h"
#include "infra/CustomerAccountStore.h"
#include "service/SessionManager.h"

namespace bms::service {

struct SystemStatus {
    bool applicationRunning = true;
    bool credentialsFileAccessible = false;
    bool customersFileAccessible = false;
    bool accountsFileAccessible = false;
    std::size_t credentialCount = 0;
    std::size_t customerCount = 0;
    std::size_t accountCount = 0;
};

class SystemStatusService {
public:
    SystemStatusService(
        infra::CredentialStore& credentialStore,
        infra::CustomerAccountStore& customerAccountStore,
        std::string credentialsPath,
        std::string customersPath,
        std::string accountsPath
    );

    domain::Status getStatus(
        const SessionManager& session,
        SystemStatus& outStatus
    ) const;

private:
    infra::CredentialStore& credentialStore_;
    infra::CustomerAccountStore& customerAccountStore_;
    std::string credentialsPath_;
    std::string customersPath_;
    std::string accountsPath_;
};

} // namespace bms::service
