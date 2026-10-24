#include "infra/TransactionRepository.h"

#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <utility>

#include "infra/DataFormat.h"
#include "infra/FileUtil.h"

namespace bms::infra {

namespace {

bool parseTxnNumber(
    const std::string& id,
    long long& number) {
    if (id.size() < 4 ||
        id.compare(0, 3, "TXN") != 0) {
        return false;
    }

    return format::parseNonNegative(id.substr(3), number);
}

std::string toLine(const domain::Transaction& t) {
    return format::join({
        t.txnId,
        domain::toString(t.type),
        t.accountNo,
        t.counterparty,
        std::to_string(t.amountPaise),
        std::to_string(t.balanceAfterPaise),
        t.timestamp,
        domain::toString(t.state)
    });
}

} // namespace

LoadStatus TransactionRepository::load() {
    records_.clear();
    error_.clear();

    std::string contents;
    if (!readAll(path_, contents)) {
        return LoadStatus::MISSING;
    }

    std::istringstream in(contents);
    std::string line;
    int lineNo = 0;
    std::vector<domain::Transaction> parsed;

    while (std::getline(in, line)) {
        ++lineNo;

        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (line.empty()) continue;

        const std::string at =
            path_ + " line " + std::to_string(lineNo) + ": ";

        const auto f = format::split(line);

        if (f.size() != format::kTransactionFields) {
            error_ = at + "expected 8 fields but found " +
                     std::to_string(f.size());
            return LoadStatus::CORRUPT;
        }

        domain::Transaction t;
        long long number = 0;

        if (!parseTxnNumber(f[0], number)) {
            error_ = at + "bad transaction id";
            return LoadStatus::CORRUPT;
        }

        t.txnId = f[0];

        if (!domain::txnTypeFromString(f[1], t.type)) {
            error_ = at + "bad type";
            return LoadStatus::CORRUPT;
        }

        if (f[2].empty()) {
            error_ = at + "account number is empty";
            return LoadStatus::CORRUPT;
        }

        t.accountNo = f[2];
        t.counterparty = f[3];

        if (!format::parseNonNegative(
                f[4], t.amountPaise) ||
            t.amountPaise == 0) {
            error_ = at +
                "amount must be a positive whole number of paise";
            return LoadStatus::CORRUPT;
        }

        if (!format::parseNonNegative(
                f[5], t.balanceAfterPaise)) {
            error_ = at +
                "balanceAfter must be a non-negative whole number";
            return LoadStatus::CORRUPT;
        }

        if (!format::isTimestamp(f[6])) {
            error_ = at + "bad timestamp";
            return LoadStatus::CORRUPT;
        }

        t.timestamp = f[6];

        if (!domain::txnStateFromString(f[7], t.state)) {
            error_ = at + "bad state";
            return LoadStatus::CORRUPT;
        }

        parsed.push_back(t);
    }

    records_ = std::move(parsed);
    return LoadStatus::OK;
}

bool TransactionRepository::append(
    const domain::Transaction& txn) {
    {
        std::ofstream out(
            path_, std::ios::app | std::ios::binary);

        if (!out) return false;

        out << toLine(txn) << '\n';
        out.flush();

        if (!out) return false;
    }

    records_.push_back(txn);
    return true;
}

std::string TransactionRepository::nextTxnId() const {
    long long maxNumber = 0;

    for (const auto& t : records_) {
        long long number = 0;

        if (parseTxnNumber(t.txnId, number) &&
            number > maxNumber) {
            maxNumber = number;
        }
    }

    char buf[32];
    std::snprintf(
        buf, sizeof(buf), "TXN%06lld", maxNumber + 1);

    return buf;
}

std::vector<domain::Transaction>
TransactionRepository::pending() const {
    std::map<std::string, std::size_t> lastIndex;

    for (std::size_t i = 0; i < records_.size(); ++i) {
        lastIndex[records_[i].txnId] = i;
    }

    std::vector<domain::Transaction> result;

    for (std::size_t i = 0; i < records_.size(); ++i) {
        if (lastIndex[records_[i].txnId] == i &&
            records_[i].state == domain::TxnState::PENDING) {
            result.push_back(records_[i]);
        }
    }

    return result;
}

} // namespace bms::infra