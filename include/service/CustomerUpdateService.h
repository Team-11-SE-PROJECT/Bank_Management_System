#pragma once

#include "domain/Customer.h"
#include "domain/Status.h"
#include "infra/CustomerAccountStore.h"
#include "service/SessionManager.h"

namespace bms::service {

class CustomerUpdateService {
public:
    explicit CustomerUpdateService(
        infra::CustomerAccountStore& customerAccountStore
    );

    domain::Status updateCustomer(
        const SessionManager& session,
        const domain::Customer& customer
    );

private:
    infra::CustomerAccountStore& customerAccountStore_;
};

} // namespace bms::service