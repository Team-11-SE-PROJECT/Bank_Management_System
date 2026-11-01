#pragma once

#include <string>

#include "domain/Status.h"
#include "infra/AccountRepository.h"
#include "infra/AuditLogger.h"
#include "service/SessionManager.h"

namespace bms::service {

class AccountClosureService {
public:
    explicit AccountClosureService(
        infra::AccountRepository& accounts,
        const infra::AuditLogger* auditLogger = nullptr
    );

    domain::Status closeAccount(
        const SessionManager& session,
        const std::string& accountNo
    );

private:
    infra::AccountRepository& accounts_;
    const infra::AuditLogger* auditLogger_ = nullptr;
};

} // namespace bms::service
