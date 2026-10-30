#pragma once

#include <string>
#include <utility>
#include <vector>

#include "domain/Transaction.h"
#include "infra/LoadStatus.h"

namespace bms::infra {

class TransactionRepository {
public:
    explicit TransactionRepository(std::string path)
        : path_(std::move(path)) {}

    LoadStatus load();
    bool append(const domain::Transaction& txn);
    bool saveAll() const;

    const std::string& lastError() const {
        return error_;
    }

    const LoadStatus& lastStatus() const {
        return status_;
    }

    const std::vector<domain::Transaction>& all() const {
        return records_;
    }

    std::string nextTxnId() const;

    std::vector<domain::Transaction> pending() const;

    const std::string& path() const {
        return path_;
    }

private:
    std::string path_;
    std::vector<domain::Transaction> records_;
    std::string error_;
    LoadStatus status_;
};

} // namespace bms::infra
