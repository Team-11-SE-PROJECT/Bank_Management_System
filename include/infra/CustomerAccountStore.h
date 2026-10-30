#pragma once

#include <string>
#include <vector>

#include "domain/Account.h"
#include "domain/Customer.h"
#include "infra/LoadStatus.h"

namespace bms::infra {

class CustomerAccountStore {
public:
    CustomerAccountStore(
        std::string customersPath,
        std::string accountsPath
    );

    bool load();
    LoadStatus loadWithStatus();

    const LoadStatus& lastStatus() const {
        return lastStatus_;
    }

    bool saveCustomers() const;
    bool saveAccounts() const;

    const std::vector<domain::Customer>& customers() const;
    const std::vector<domain::Account>& accounts() const;

    const domain::Customer* findCustomer(
        const std::string& customerId
    ) const;

    const domain::Account* findAccount(
        const std::string& accountNo
    ) const;

    bool addCustomer(const domain::Customer& customer);
    bool addAccount(const domain::Account& account);

    bool customerExists(const std::string& customerId) const;
    bool accountExists(const std::string& accountNo) const;

    const std::string& customersPath() const { return customersPath_; }
    const std::string& accountsPath() const { return accountsPath_; }

private:
    std::string customersPath_;
    std::string accountsPath_;

    std::vector<domain::Customer> customers_;
    std::vector<domain::Account> accounts_;
    LoadStatus lastStatus_;
};

} // namespace bms::infra
