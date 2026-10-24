
#pragma once

#include <string>

#include "domain/Status.h"
#include "infra/CredentialStore.h"
#include "infra/CustomerAccountStore.h"

namespace bms::service {

struct CustomerInfo {
    std::string name;
    std::string phone;
    std::string address;
    std::string initialPin;
};

class AccountService {
public:
    AccountService(
        infra::CustomerAccountStore& customerAccountStore,
        infra::CredentialStore& credentialStore
    );

    domain::Status createAccount(
        const CustomerInfo& customerInfo,
        std::string& outAccountNo
    );

private:
    infra::CustomerAccountStore& customerAccountStore_;
    infra::CredentialStore& credentialStore_;
};

} // namespace bms::service
