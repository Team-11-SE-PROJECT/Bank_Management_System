
#include "infra/CustomerAccountStore.h"

#include <sstream>
#include <unordered_set>
#include <utility>

#include "infra/FileUtil.h"
#include "infra/InputValidator.h"

namespace bms::infra {

namespace {

bool validField(const std::string& value, bool required = true) {
    if (required && value.empty()) {
        return false;
    }

    return value.find('|') == std::string::npos &&
           value.find('\n') == std::string::npos &&
           value.find('\r') == std::string::npos;
}

bool validCustomer(const domain::Customer& customer) {
    return validField(customer.customerId) &&
           validField(customer.name) &&
           validField(customer.phone) &&
           validField(customer.address, false);
}

bool validAccount(const domain::Account& account) {
    return validField(account.accountNo) &&
           validField(account.customerId) &&
           (account.status == "ACTIVE" ||
            account.status == "CLOSED") &&
           account.balancePaise >= 0 &&
           account.balancePaise <= kMaxAmountPaise &&
           account.withdrawnTodayPaise >= 0 &&
           account.withdrawnTodayPaise <= kMaxAmountPaise &&
           validField(account.lastWithdrawDate, false);
}

} // namespace

CustomerAccountStore::CustomerAccountStore(
    std::string customersPath,
    std::string accountsPath
)
    : customersPath_(std::move(customersPath)),
      accountsPath_(std::move(accountsPath)) {}

bool CustomerAccountStore::load() {
    std::vector<domain::Customer> loadedCustomers;
    std::vector<domain::Account> loadedAccounts;
    std::string contents;

    std::unordered_set<std::string> customerIds;
    std::unordered_set<std::string> accountNumbers;

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
                !validCustomer(customer) ||
                !customerIds.insert(customer.customerId).second) {
                return false;
            }

            loadedCustomers.push_back(customer);
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
                !std::getline(row, withdrawn, '|')) {
                return false;
            }

            std::getline(row, account.lastWithdrawDate);

            try {
                std::size_t balanceEnd = 0;
                std::size_t withdrawnEnd = 0;

                account.balancePaise =
                    std::stoll(balance, &balanceEnd);
                account.withdrawnTodayPaise =
                    std::stoll(withdrawn, &withdrawnEnd);

                if (balanceEnd != balance.size() ||
                    withdrawnEnd != withdrawn.size()) {
                    return false;
                }
            } catch (...) {
                return false;
            }

            if (!validAccount(account) ||
                !customerIds.count(account.customerId) ||
                !accountNumbers.insert(account.accountNo).second) {
                return false;
            }

            loadedAccounts.push_back(account);
        }
    }

    customers_ = std::move(loadedCustomers);
    accounts_ = std::move(loadedAccounts);
    return true;
}

bool CustomerAccountStore::saveCustomers() const {
    std::string output;

    for (const auto& customer : customers_) {
        if (!validCustomer(customer)) {
            return false;
        }

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
        if (!validAccount(account) ||
            !customerExists(account.customerId)) {
            return false;
        }

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

const domain::Customer* CustomerAccountStore::findCustomer(
    const std::string& customerId
) const {
    for (const auto& customer : customers_) {
        if (customer.customerId == customerId) {
            return &customer;
        }
    }

    return nullptr;
}

const domain::Account* CustomerAccountStore::findAccount(
    const std::string& accountNo
) const {
    for (const auto& account : accounts_) {
        if (account.accountNo == accountNo) {
            return &account;
        }
    }

    return nullptr;
}

bool CustomerAccountStore::addCustomer(
    const domain::Customer& customer
) {
    if (!validCustomer(customer) ||
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
    if (!validAccount(account) ||
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
    return findCustomer(customerId) != nullptr;
}

bool CustomerAccountStore::accountExists(
    const std::string& accountNo
) const {
    return findAccount(accountNo) != nullptr;
}

} // namespace bms::infra
