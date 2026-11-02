#ifndef BMS_CUSTOMER_SEARCH_SERVICE_H
#define BMS_CUSTOMER_SEARCH_SERVICE_H

#include <string>
#include <vector>

#include "domain/Account.h"
#include "domain/Customer.h"
#include "domain/Status.h"
#include "infra/CustomerAccountStore.h"
#include "service/SessionManager.h"

namespace bms {
namespace service {

enum class CustomerSearchField {
    CUSTOMER_ID,
    CUSTOMER_NAME,
    PHONE,
    ACCOUNT_NUMBER
};

struct CustomerSearchRecord {
    domain::Customer customer;
    std::vector<domain::Account> accounts;
};

class CustomerSearchService {
public:
    explicit CustomerSearchService(
        infra::CustomerAccountStore& customerData
    );

    domain::Status search(
        const SessionManager& session,
        CustomerSearchField field,
        const std::string& query,
        std::vector<CustomerSearchRecord>& results
    ) const;

private:
    infra::CustomerAccountStore& customerData;
};

} // namespace service
} // namespace bms

#endif