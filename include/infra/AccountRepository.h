#pragma once

#include <string>
#include <utility>
#include <vector>

#include "domain/Account.h"
#include "infra/LoadStatus.h"

namespace bms::infra {

class AccountRepository {
public:
    explicit AccountRepository(std::string path)
        : path_(std::move(path)) {}

    LoadStatus load();
    bool save() const;

    const std::string& lastError() const {
        return error_;
    }

    const domain::Account* find(
        const std::string& accountNo) const;

    domain::Account* find(
        const std::string& accountNo);

    const domain::Account* findByCustomer(
        const std::string& customerId) const;

    void upsert(const domain::Account& account);

    const std::vector<domain::Account>& all() const {
        return records_;
    }

    const std::string& path() const {
        return path_;
    }

private:
    std::string path_;
    std::vector<domain::Account> records_;
    std::string error_;
};

} // namespace bms::infra