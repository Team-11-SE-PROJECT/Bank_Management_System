#include "infra/CustomerAccountStore.h"

#include <filesystem>
#include <sstream>
#include <unordered_set>
#include <utility>

#include "infra/DataFormat.h"
#include "infra/FileUtil.h"

namespace fs = std::filesystem;

namespace bms::infra {

namespace {

void stripTrailingCr(std::string& line) {
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
}

} // namespace

CustomerAccountStore::CustomerAccountStore(
    std::string customersPath,
    std::string accountsPath
)
    : customersPath_(std::move(customersPath)),
      accountsPath_(std::move(accountsPath)) {}

bool CustomerAccountStore::load() {
    return loadWithStatus().isOk();
}

LoadStatus CustomerAccountStore::loadWithStatus() {
    std::vector<domain::Customer> loadedCustomers;
    std::vector<domain::Account> loadedAccounts;
    std::string contents;

    std::unordered_set<std::string> customerIds;
    std::unordered_set<std::string> accountNumbers;

    lastStatus_ = LoadStatus::OK;

    // 1. Load customers file
    std::error_code ec;
    if (fs::exists(customersPath_, ec)) {
        if (!readAll(customersPath_, contents)) {
            lastStatus_ = LoadStatus(LoadErrorCode::IO_ERROR,
                "Cannot read customers file: " + customersPath_, 0, customersPath_);
            return lastStatus_;
        }

        std::istringstream input(contents);
        std::string line;
        int lineNo = 0;
        bool checkedHeader = false;

        while (std::getline(input, line)) {
            ++lineNo;
            stripTrailingCr(line);
            if (line.empty()) continue;

            if (!checkedHeader && format::isVersionHeader(line)) {
                checkedHeader = true;
                int ver = 0;
                std::string err;
                if (!format::parseVersionHeader(line, ver, err)) {
                    std::string quar;
                    quarantineFile(customersPath_, quar);
                    lastStatus_ = LoadStatus(LoadErrorCode::MALFORMED,
                        "Malformed version header: " + err, lineNo, customersPath_, quar);
                    return lastStatus_;
                }
                if (ver > format::kCustomerVersion) {
                    lastStatus_ = LoadStatus(LoadErrorCode::UNSUPPORTED_VERSION,
                        "Unsupported customer version: " + std::to_string(ver), lineNo, customersPath_);
                    return lastStatus_;
                }
                continue;
            }
            checkedHeader = true;

            domain::Customer customer;
            auto status = format::deserializeCustomer(line, customer, lineNo);
            if (!status.isOk()) {
                std::string quar;
                quarantineFile(customersPath_, quar);
                lastStatus_ = LoadStatus(LoadErrorCode::MALFORMED,
                    status.errorMessage, lineNo, customersPath_, quar);
                return lastStatus_;
            }

            if (!customerIds.insert(customer.customerId).second) {
                std::string quar;
                quarantineFile(customersPath_, quar);
                lastStatus_ = LoadStatus(LoadErrorCode::MALFORMED,
                    "Duplicate customer ID: " + customer.customerId, lineNo, customersPath_, quar);
                return lastStatus_;
            }

            loadedCustomers.push_back(customer);
        }
    }

    // 2. Load accounts file
    if (fs::exists(accountsPath_, ec)) {
        contents.clear();
        if (!readAll(accountsPath_, contents)) {
            lastStatus_ = LoadStatus(LoadErrorCode::IO_ERROR,
                "Cannot read accounts file: " + accountsPath_, 0, accountsPath_);
            return lastStatus_;
        }

        std::istringstream input(contents);
        std::string line;
        int lineNo = 0;
        bool checkedHeader = false;

        while (std::getline(input, line)) {
            ++lineNo;
            stripTrailingCr(line);
            if (line.empty()) continue;

            if (!checkedHeader && format::isVersionHeader(line)) {
                checkedHeader = true;
                int ver = 0;
                std::string err;
                if (!format::parseVersionHeader(line, ver, err)) {
                    std::string quar;
                    quarantineFile(accountsPath_, quar);
                    lastStatus_ = LoadStatus(LoadErrorCode::MALFORMED,
                        "Malformed version header: " + err, lineNo, accountsPath_, quar);
                    return lastStatus_;
                }
                if (ver > format::kAccountVersion) {
                    lastStatus_ = LoadStatus(LoadErrorCode::UNSUPPORTED_VERSION,
                        "Unsupported account version: " + std::to_string(ver), lineNo, accountsPath_);
                    return lastStatus_;
                }
                continue;
            }
            checkedHeader = true;

            domain::Account account;
            auto status = format::deserializeAccount(line, account, lineNo);
            if (!status.isOk()) {
                std::string quar;
                quarantineFile(accountsPath_, quar);
                lastStatus_ = LoadStatus(LoadErrorCode::MALFORMED,
                    status.errorMessage, lineNo, accountsPath_, quar);
                return lastStatus_;
            }

            // Cross-reference integrity: customerId must exist in loaded customers
            if (customerIds.find(account.customerId) == customerIds.end()) {
                std::string quar;
                quarantineFile(accountsPath_, quar);
                lastStatus_ = LoadStatus(LoadErrorCode::MALFORMED,
                    "Account " + account.accountNo + " references non-existent customer: " + account.customerId,
                    lineNo, accountsPath_, quar);
                return lastStatus_;
            }

            // Duplicate account number check
            if (!accountNumbers.insert(account.accountNo).second) {
                std::string quar;
                quarantineFile(accountsPath_, quar);
                lastStatus_ = LoadStatus(LoadErrorCode::MALFORMED,
                    "Duplicate account number: " + account.accountNo, lineNo, accountsPath_, quar);
                return lastStatus_;
            }

            loadedAccounts.push_back(account);
        }
    }

    customers_ = std::move(loadedCustomers);
    accounts_ = std::move(loadedAccounts);
    lastStatus_ = LoadStatus::OK;
    return lastStatus_;
}

bool CustomerAccountStore::saveCustomers() const {
    std::string output = format::makeVersionHeader(format::kCustomerVersion) + "\n";

    for (const auto& customer : customers_) {
        std::string line;
        std::string err;
        if (!format::serializeCustomer(customer, line, err)) {
            return false;
        }
        output += line + "\n";
    }

    return atomicWrite(customersPath_, output);
}

bool CustomerAccountStore::saveAccounts() const {
    std::string output = format::makeVersionHeader(format::kAccountVersion) + "\n";

    for (const auto& account : accounts_) {
        if (!customerExists(account.customerId)) {
            return false;
        }

        std::string line;
        std::string err;
        if (!format::serializeAccount(account, line, err)) {
            return false;
        }
        output += line + "\n";
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
    std::string line;
    std::string err;
    if (!format::serializeCustomer(customer, line, err) ||
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
    std::string line;
    std::string err;
    if (!format::serializeAccount(account, line, err) ||
        account.balancePaise > kMaxAmountPaise ||
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
