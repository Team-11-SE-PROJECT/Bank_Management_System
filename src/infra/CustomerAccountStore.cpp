
#include "infra/CustomerAccountStore.h"

#include <sstream>
#include <utility>

#include "infra/FileUtil.h"

namespace bms::infra {

CustomerAccountStore::CustomerAccountStore(
    std::string customersPath,
    std::string accountsPath
)
    : customersPath_(std::move(customersPath)),
      accountsPath_(std::move(accountsPath)) {}

bool CustomerAccountStore::load() {
    customers_.clear();
    accounts_.clear();

    std::string contents;

    if (readAll(customersPath_, contents)) {
        std::istringstream input(contents);
        std::string line;

        while (std::getline(input, line)) {
            if (line.empty()) {
                continue;
            }

            std::istringstream row(line);
            domain::Customer customer;

            if (!std::getline(row, customer.customerId, '|') ||
                !std::getline(row, customer.name, '|') ||
                !std::getline(row, customer.phone, '|') ||
                !std::getline(row, customer.address) ||
                customer.customerId.empty() ||
                customer.name.empty() ||
                customer.phone.empty()) {
                customers_.clear();
                return false;
            }

            customers_.push_back(customer);
        }
    }

    if (readAll(accountsPath_, contents)) {
        std::istringstream input(contents);
        std::string line;

        while (std::getline(input, line)) {
            if (line.empty()) {
                continue;
            }

            std::istringstream row(line);
            domain::Account account;
            std::string balance;
            std::string withdrawn;

            
if (!std::getline(row, account.accountNo, '|') ||
    !std::getline(row, account.customerId, '|') ||
    !std::getline(row, balance, '|') ||
    !std::getline(row, account.status, '|') ||
    !std::getline(row, withdrawn, '|') ||
    account.accountNo.empty() ||
    account.customerId.empty()) {
    accounts_.clear();
    return false;
}

std::getline(row, account.lastWithdrawDate);


            try {
                std::size_t balanceEnd = 0;
                std::size_t withdrawnEnd = 0;

                account.balancePaise = std::stoll(balance, &balanceEnd);
                account.withdrawnTodayPaise =
                    std::stoll(withdrawn, &withdrawnEnd);

                if (balanceEnd != balance.size() ||
                    withdrawnEnd != withdrawn.size()) {
                    accounts_.clear();
                    return false;
                }
            } catch (...) {
                accounts_.clear();
                return false;
            }

            accounts_.push_back(account);
        }
    }

    return true;
}

bool CustomerAccountStore::saveCustomers() const {
    std::string output;

    for (const auto& customer : customers_) {
        output += customer.customerId + "|" +
                  customer.name + "|" +
                  customer.phone + "|" +
                  customer.address + "\n";
    }

    return atomicWrite(customersPath_, output);
}

bool CustomerAccountStore::saveAccounts() const {
    std::string output;

    for (const auto& account : accounts_) {
        output += account.accountNo + "|" +
                  account.customerId + "|" +
                  std::to_string(account.balancePaise) + "|" +
                  account.status + "|" +
                  std::to_string(account.withdrawnTodayPaise) + "|" +
                  account.lastWithdrawDate + "\n";
    }

    return atomicWrite(accountsPath_, output);
}

const std::vector<domain::Customer>&
CustomerAccountStore::customers() const {
    return customers_;
}

const std::vector<domain::Account>&
CustomerAccountStore::accounts() const {
    return accounts_;
}

bool CustomerAccountStore::addCustomer(
    const domain::Customer& customer
) {
    if (customer.customerId.empty() ||
        customer.name.empty() ||
        customer.phone.empty() ||
        customerExists(customer.customerId)) {
        return false;
    }

    customers_.push_back(customer);

    if (!saveCustomers()) {
        customers_.pop_back();
        return false;
    }

    return true;
}

bool CustomerAccountStore::addAccount(
    const domain::Account& account
) {
    if (account.accountNo.empty() ||
        account.customerId.empty() ||
        !customerExists(account.customerId) ||
        accountExists(account.accountNo)) {
        return false;
    }

    accounts_.push_back(account);

    if (!saveAccounts()) {
        accounts_.pop_back();
        return false;
    }

    return true;
}

bool CustomerAccountStore::customerExists(
    const std::string& customerId
) const {
    for (const auto& customer : customers_) {
        if (customer.customerId == customerId) {
            return true;
        }
    }

    return false;
}

bool CustomerAccountStore::accountExists(
    const std::string& accountNo
) const {
    for (const auto& account : accounts_) {
        if (account.accountNo == accountNo) {
            return true;
        }
    }

    return false;
}

} // namespace bms::infra
