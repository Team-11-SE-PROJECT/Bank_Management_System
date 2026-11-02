#pragma once

#include <memory>
#include <string>

#include "infra/AccountRepository.h"
#include "infra/AuditLogger.h"
#include "infra/Config.h"
#include "infra/CredentialStore.h"
#include "infra/CustomerAccountStore.h"
#include "infra/LoginService.h"
#include "infra/TransactionRepository.h"
#include "service/AccountService.h"
#include "service/AccountUnlockService.h"
#include "service/AuthService.h"
#include "service/SystemStatusService.h"
#include "service/TransactionService.h"

namespace bms::service {

class LocalWebServer {
public:
    LocalWebServer(
        infra::LoginService& login,
        infra::CredentialStore& credentials,
        infra::AccountRepository& accounts,
        infra::TransactionRepository& transactions,
        infra::CustomerAccountStore& customerData,
        TransactionService& transactionService,
        AccountService& accountService,
        AccountUnlockService& unlockService,
        SystemStatusService& systemStatusService,
        AuthService& authService
    );
    ~LocalWebServer();

    bool serve(unsigned short port);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace bms::service
