#include "service/SystemStatusService.h"

#include <fstream>

#include "service/RbacGuard.h"

namespace bms::service {

SystemStatusService::SystemStatusService(
    infra::CredentialStore& credentialStore,
    infra::CustomerAccountStore& customerAccountStore,
    std::string credentialsPath,
    std::string customersPath,
    std::string accountsPath
)
    : credentialStore_(credentialStore),
      customerAccountStore_(customerAccountStore),
      credentialsPath_(std::move(credentialsPath)),
      customersPath_(std::move(customersPath)),
      accountsPath_(std::move(accountsPath)) {}

domain::Status SystemStatusService::getStatus(
    const SessionManager& session,
    SystemStatus& outStatus
) const {
    if (RbacGuard::checkRole(session, infra::Role::ADMIN) !=
        domain::Status::OK) {
        return domain::Status::UNAUTHORIZED;
    }

    SystemStatus status;

    std::ifstream credentialsFile(credentialsPath_, std::ios::binary);
    status.credentialsFileAccessible = credentialsFile.good();

    std::ifstream customersFile(customersPath_, std::ios::binary);
    status.customersFileAccessible = customersFile.good();

    std::ifstream accountsFile(accountsPath_, std::ios::binary);
    status.accountsFileAccessible = accountsFile.good();

    status.credentialCount = credentialStore_.all().size();
    status.customerCount = customerAccountStore_.customers().size();
    status.accountCount = customerAccountStore_.accounts().size();

    outStatus = status;
    return domain::Status::OK;
}

} // namespace bms::service
