#include "service/CustomerSearchService.h"

#include <algorithm>
#include <cctype>
#include <utility>

#include "infra/LoginService.h"
#include "service/RbacGuard.h"

namespace bms {
namespace service {

namespace {

std::string toLower(const std::string& value) {
    std::string result = value;

    std::transform(
        result.begin(),
        result.end(),
        result.begin(),
        [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        }
    );

    return result;
}

} // namespace

CustomerSearchService::CustomerSearchService(
    infra::CustomerAccountStore& customerData
)
    : customerData(customerData) {
}

domain::Status CustomerSearchService::search(
    const SessionManager& session,
    CustomerSearchField field,
    const std::string& query,
    std::vector<CustomerSearchRecord>& results
) const {
    results.clear();

    if (query.empty()) {
        return domain::Status::INVALID_INPUT;
    }

    const auto accessStatus =
        RbacGuard::checkRole(session, infra::Role::ADMIN);

    if (accessStatus != domain::Status::OK) {
        return accessStatus;
    }

    const auto& customers = customerData.customers();
    const auto& accounts = customerData.accounts();

    const std::string normalizedQuery = toLower(query);

    if (field == CustomerSearchField::ACCOUNT_NUMBER) {
        const auto* account = customerData.findAccount(query);

        if (account == nullptr) {
            return domain::Status::NOT_FOUND;
        }

        const auto* customer =
            customerData.findCustomer(account->customerId);

        if (customer == nullptr) {
            return domain::Status::NOT_FOUND;
        }

        CustomerSearchRecord record;
        record.customer = *customer;
        record.accounts.push_back(*account);
        results.push_back(std::move(record));

        return domain::Status::OK;
    }

    for (const auto& customer : customers) {
        bool matches = false;

        switch (field) {
            case CustomerSearchField::CUSTOMER_ID:
                matches = customer.customerId == query;
                break;

            case CustomerSearchField::CUSTOMER_NAME:
                matches =
                    toLower(customer.name).find(normalizedQuery)
                    != std::string::npos;
                break;

            case CustomerSearchField::PHONE:
                matches = customer.phone == query;
                break;

            case CustomerSearchField::ACCOUNT_NUMBER:
                break;
        }

        if (!matches) {
            continue;
        }

        CustomerSearchRecord record;
        record.customer = customer;

        for (const auto& account : accounts) {
            if (account.customerId == customer.customerId) {
                record.accounts.push_back(account);
            }
        }

        results.push_back(std::move(record));
    }

    return results.empty()
        ? domain::Status::NOT_FOUND
        : domain::Status::OK;
}

} // namespace service
} // namespace bms