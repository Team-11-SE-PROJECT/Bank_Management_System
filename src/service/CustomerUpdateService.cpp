#include "service/CustomerUpdateService.h"

#include "service/RbacGuard.h"

namespace bms::service {

CustomerUpdateService::CustomerUpdateService(
    infra::CustomerAccountStore& customerAccountStore
) : customerAccountStore_(customerAccountStore) {}

domain::Status CustomerUpdateService::updateCustomer(
    const SessionManager& session,
    const domain::Customer& customer
) {
    if (!session.isActive()) {
        return domain::Status::UNAUTHORIZED;
    }

    if (customer.customerId.empty() ||
        customer.name.empty() ||
        customer.phone.empty() ||
        customer.address.empty()) {
        return domain::Status::INVALID_INPUT;
    }

    const infra::Role role = session.getRole();

    if (role == infra::Role::CUSTOMER) {
        if (RbacGuard::checkRole(session, infra::Role::CUSTOMER) !=
                domain::Status::OK ||
            session.getUserId() != customer.customerId) {
            return domain::Status::UNAUTHORIZED;
        }
    } else if (role == infra::Role::ADMIN) {
        if (RbacGuard::checkRole(session, infra::Role::ADMIN) !=
            domain::Status::OK) {
            return domain::Status::UNAUTHORIZED;
        }
    } else {
        return domain::Status::UNAUTHORIZED;
    }

    if (customerAccountStore_.findCustomer(customer.customerId) == nullptr) {
        return domain::Status::NOT_FOUND;
    }

    if (!customerAccountStore_.updateCustomer(customer)) {
        return domain::Status::STORAGE_FAILURE;
    }

    return domain::Status::OK;
}

} // namespace bms::service