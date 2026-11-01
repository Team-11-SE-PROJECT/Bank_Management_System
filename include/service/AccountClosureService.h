#pragma once

#include <string>

#include "domain/Status.h"
#include "infra/AccountRepository.h"
#include "service/SessionManager.h"

namespace bms::service {

class AccountClosureService {
public:
    explicit AccountClosureService(
        infra::AccountRepository& accounts
    );

    domain::Status closeAccount(
        const SessionManager& session,
        const std::string& accountNo
    );

private:
    infra::AccountRepository& accounts_;
};

} // namespace bms::service
