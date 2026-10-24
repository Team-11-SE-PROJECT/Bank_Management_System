
#include "service/AccountService.h"

#include <cctype>
#include <string>

#include "infra/PasswordHasher.h"
#include "service/AccountNumberGenerator.h"

namespace bms::service {

namespace {

bool isValidPin(const std::string& pin) {
    if (pin.size() < 4 || pin.size() > 6) {
        return false;
    }

    for (unsigned char ch : pin) {
        if (!std::isdigit(ch)) {
            return false;
        }
    }

    return true;
}

bool isValidPhone(const std::string& phone) {
    if (phone.size() != 10) {
        return false;
    }

    for (unsigned char ch : phone) {
        if (!std::isdigit(ch)) {
            return false;
        }
    }

    return true;
}

bool hasPipeOrNewline(const std::string& value) {
    return value.find('|') != std::string::npos ||
           value.find('\n') != std::string::npos ||
           value.find('\r') != std::string::npos;
}

std::string nextCustomerId(
    const infra::CustomerAccountStore& store
) {
    unsigned long long sequence = 1;

    for (const auto& customer : store.customers()) {
        if (customer.customerId.rfind("CUST", 0) == 0) {
            try {
                const auto number =
                    std::stoull(customer.customerId.substr(4));

                if (number >= sequence) {
                    sequence = number + 1;
                }
            } catch (...) {
                // Ignore IDs that do not have a numeric suffix.
            }
        }
    }

    return "CUST" + std::string(
        sequence < 10 ? "000" :
        sequence < 100 ? "00" :
        sequence < 1000 ? "0" : ""
    ) + std::to_string(sequence);
}

unsigned long long nextAccountSequence(
    const infra::CustomerAccountStore& store
) {
    unsigned long long sequence = 1;

    for (const auto& account : store.accounts()) {
        if (account.accountNo.rfind("BMS", 0) == 0) {
            try {
                const auto number =
                    std::stoull(account.accountNo.substr(3));

                if (number >= sequence) {
                    sequence = number + 1;
                }
            } catch (...) {
                // Ignore account numbers with invalid numeric suffixes.
            }
        }
    }

    return sequence;
}

} // namespace

AccountService::AccountService(
    infra::CustomerAccountStore& customerAccountStore,
    infra::CredentialStore& credentialStore
)
    : customerAccountStore_(customerAccountStore),
      credentialStore_(credentialStore) {}

domain::Status AccountService::createAccount(
    const CustomerInfo& customerInfo,
    std::string& outAccountNo
) {
    outAccountNo.clear();

    if (customerInfo.name.empty() ||
        customerInfo.phone.empty() ||
        customerInfo.address.empty() ||
        hasPipeOrNewline(customerInfo.name) ||
        hasPipeOrNewline(customerInfo.phone) ||
        hasPipeOrNewline(customerInfo.address) ||
        !isValidPhone(customerInfo.phone) ||
        !isValidPin(customerInfo.initialPin)) {
        return domain::Status::INVALID_INPUT;
    }

    if (customerAccountStore_.load() == false) {
        return domain::Status::STORAGE_FAILURE;
    }

    if (credentialStore_.load() == false) {
        return domain::Status::STORAGE_FAILURE;
    }

    const std::string customerId =
        nextCustomerId(customerAccountStore_);

    if (credentialStore_.find(customerId) != nullptr) {
        return domain::Status::STORAGE_FAILURE;
    }

    const std::string accountNo =
        AccountNumberGenerator::generate(
            nextAccountSequence(customerAccountStore_)
        );

    if (customerAccountStore_.accountExists(accountNo)) {
        return domain::Status::STORAGE_FAILURE;
    }

    infra::Credential credential;
    credential.userId = customerId;
    credential.role = infra::Role::CUSTOMER;
    credential.salt = infra::PasswordHasher::generateSalt();
    credential.hashHex = infra::PasswordHasher::hash(
        customerInfo.initialPin,
        credential.salt
    );

    domain::Customer customer{
        customerId,
        customerInfo.name,
        customerInfo.phone,
        customerInfo.address
    };

    domain::Account account;
    account.accountNo = accountNo;
    account.customerId = customerId;
    account.balancePaise = 0;
    account.status = "ACTIVE";
    account.withdrawnTodayPaise = 0;
    account.lastWithdrawDate = "";

    if (!customerAccountStore_.addCustomer(customer)) {
        return domain::Status::STORAGE_FAILURE;
    }

    if (!customerAccountStore_.addAccount(account)) {
        return domain::Status::STORAGE_FAILURE;
    }

    credentialStore_.upsert(credential);

    if (!credentialStore_.save()) {
        return domain::Status::STORAGE_FAILURE;
    }

    outAccountNo = accountNo;
    return domain::Status::OK;
}

} // namespace bms::service
