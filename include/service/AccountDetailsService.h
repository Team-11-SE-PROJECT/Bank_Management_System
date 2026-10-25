
#pragma once

#include <string>

#include "domain/Account.h"
#include "domain/Customer.h"
#include "infra/CustomerAccountStore.h"

namespace bms::service {

enum class AccountDetailsStatus {
    OK,
    INVALID_INPUT,
    NOT_FOUND,
    UNAUTHORIZED
};

struct AccountDetailsResult {
    AccountDetailsStatus status = AccountDetailsStatus::NOT_FOUND;
    domain::Customer customer;
    domain::Account account;
};

class AccountDetailsService {
public:
    explicit AccountDetailsService(
        const infra::CustomerAccountStore& store
    );

    AccountDetailsResult getByAccountNumber(
        const std::string& accountNo,
        const std::string& requestingCustomerId
    ) const;

private:
    const infra::CustomerAccountStore& store_;
};

} // namespace bms::service
