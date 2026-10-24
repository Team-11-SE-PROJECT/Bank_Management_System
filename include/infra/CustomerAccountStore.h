
#pragma once

#include <string>
#include <vector>

#include "domain/Account.h"
#include "domain/Customer.h"

namespace bms::infra {

class CustomerAccountStore {
public:
    CustomerAccountStore(
        std::string customersPath,
        std::string accountsPath
    );

    bool load();

    bool saveCustomers() const;
    bool saveAccounts() const;

    const std::vector<domain::Customer>& customers() const;
    const std::vector<domain::Account>& accounts() const;

    bool addCustomer(const domain::Customer& customer);
    bool addAccount(const domain::Account& account);

    bool customerExists(const std::string& customerId) const;
    bool accountExists(const std::string& accountNo) const;

private:
    std::string customersPath_;
    std::string accountsPath_;

    std::vector<domain::Customer> customers_;
    std::vector<domain::Account> accounts_;
};

} // namespace bms::infra
