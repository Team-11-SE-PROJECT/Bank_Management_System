#include "service/AccountClosureService.h"

#include "service/RbacGuard.h"

namespace bms::service {

AccountClosureService::AccountClosureService(
    infra::AccountRepository& accounts
)
    : accounts_(accounts) {
}

domain::Status AccountClosureService::closeAccount(
    const SessionManager& session,
    const std::string& accountNo
) {
    if (accountNo.empty()) {
        return domain::Status::INVALID_INPUT;
    }

    const auto accessStatus =
        RbacGuard::checkRole(session, infra::Role::ADMIN);

    if (accessStatus != domain::Status::OK) {
        return accessStatus;
    }

    auto* account = accounts_.find(accountNo);

    if (account == nullptr) {
        return domain::Status::NOT_FOUND;
    }

    if (account->status != "ACTIVE") {
        return domain::Status::INVALID_INPUT;
    }

    if (account->balancePaise != 0) {
        return domain::Status::INVALID_INPUT;
    }

    const domain::Account previous = *account;

    domain::Account closedAccount = previous;
    closedAccount.status = "CLOSED";

    accounts_.upsert(closedAccount);

    if (!accounts_.save()) {
        accounts_.upsert(previous);
        return domain::Status::STORAGE_FAILURE;
    }

    return domain::Status::OK;
}

} // namespace bms::service
