
#include "service/AccountDetailsService.h"

namespace bms::service {

AccountDetailsService::AccountDetailsService(
    const infra::CustomerAccountStore& store
) : store_(store) {}

AccountDetailsResult AccountDetailsService::getByAccountNumber(
    const std::string& accountNo,
    const std::string& requestingCustomerId
) const {
    if (accountNo.empty() || requestingCustomerId.empty()) {
        return {AccountDetailsStatus::INVALID_INPUT, {}, {}};
    }

    const auto* account = store_.findAccount(accountNo);

    if (account == nullptr) {
        return {AccountDetailsStatus::NOT_FOUND, {}, {}};
    }

    if (account->customerId != requestingCustomerId) {
        return {AccountDetailsStatus::UNAUTHORIZED, {}, {}};
    }

    const auto* customer = store_.findCustomer(account->customerId);

    if (customer == nullptr) {
        return {AccountDetailsStatus::NOT_FOUND, {}, {}};
    }

    return {AccountDetailsStatus::OK, *customer, *account};
}

} // namespace bms::service
