#include "infra/TransactionRepository.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <utility>

#include "infra/DataFormat.h"
#include "infra/FileUtil.h"

namespace fs = std::filesystem;

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

} // namespace

LoadStatus TransactionRepository::load() {
    records_.clear();
    error_.clear();
    status_ = LoadStatus::OK;

    std::error_code ec;
    if (!fs::exists(path_, ec)) {
        status_ = LoadStatus(LoadErrorCode::MISSING, "File not found: " + path_, 0, path_);
        return status_;
    }

    std::string contents;
    if (!readAll(path_, contents)) {
        error_ = "Cannot read file (access denied or unreadable): " + path_;
        status_ = LoadStatus(LoadErrorCode::IO_ERROR, error_, 0, path_);
        return status_;
    }

    if (contents.empty()) {
        status_ = LoadStatus::OK;
        return status_;
    }

    const bool missingTrailingNewline = (contents.back() != '\n');

    std::istringstream in(contents);
    std::string line;
    int lineNo = 0;
    std::vector<domain::Transaction> parsed;
    bool checkedHeader = false;

    while (std::getline(in, line)) {
        ++lineNo;

        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (line.empty()) continue;

        if (!checkedHeader && format::isVersionHeader(line)) {
            checkedHeader = true;
            int version = 0;
            std::string verErr;
            if (!format::parseVersionHeader(line, version, verErr)) {
                error_ = path_ + " line " + std::to_string(lineNo) + ": " + verErr;
                std::string quarantine;
                quarantineFile(path_, quarantine);
                status_ = LoadStatus(LoadErrorCode::MALFORMED, error_, lineNo, path_, quarantine);
                return status_;
            }
            if (version > format::kTransactionVersion) {
                error_ = path_ + " line " + std::to_string(lineNo) + ": Unsupported version " + std::to_string(version);
                status_ = LoadStatus(LoadErrorCode::UNSUPPORTED_VERSION, error_, lineNo, path_);
                return status_;
            }
            continue;
        }
        checkedHeader = true;

        domain::Transaction t;
        auto parseStatus = format::deserializeTransaction(line, t, lineNo);
        if (!parseStatus.isOk()) {
            if (missingTrailingNewline && in.eof()) {
                error_ = path_ + " line " + std::to_string(lineNo) + ": Truncated record: " + parseStatus.errorMessage;
            } else {
                error_ = path_ + " line " + std::to_string(lineNo) + ": " + parseStatus.errorMessage;
            }
            std::string quarantine;
            quarantineFile(path_, quarantine);
            status_ = LoadStatus(LoadErrorCode::MALFORMED, error_, lineNo, path_, quarantine);
            return status_;
        }

        parsed.push_back(t);
    }

    records_ = std::move(parsed);
    status_ = LoadStatus::OK;
    return status_;
}

bool TransactionRepository::append(
    const domain::Transaction& txn) {
    std::string line;
    std::string err;
    if (!format::serializeTransaction(txn, line, err)) {
        return false;
    }

    std::error_code ec;
    const bool isNewFile = !fs::exists(path_, ec);

    {
        std::ofstream out(
            path_, std::ios::app | std::ios::binary);

        if (!out) return false;

        if (isNewFile) {
            out << format::makeVersionHeader(format::kTransactionVersion) << '\n';
        }

        out << line << '\n';
        out.flush();

        if (!out) return false;
    }

    records_.push_back(txn);
    return true;
}

bool TransactionRepository::saveAll() const {
    std::string out = format::makeVersionHeader(format::kTransactionVersion) + "\n";
    for (const auto& t : records_) {
        std::string line;
        std::string err;
        if (!format::serializeTransaction(t, line, err)) {
            return false;
        }
        out += line + "\n";
    }
    return atomicWrite(path_, out);
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
