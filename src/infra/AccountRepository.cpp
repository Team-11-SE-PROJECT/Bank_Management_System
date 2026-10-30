#include "infra/AccountRepository.h"

#include <filesystem>
#include <sstream>
#include <utility>

#include "infra/DataFormat.h"
#include "infra/FileUtil.h"

namespace fs = std::filesystem;

namespace bms::infra {

namespace {

std::string where(
    const std::string& path,
    int lineNo,
    const std::string& reason
) {
    return path + " line " + std::to_string(lineNo) +
           ": " + reason;
}

} // namespace

LoadStatus AccountRepository::load() {
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

    // Empty file is valid (0 records)
    if (contents.empty()) {
        status_ = LoadStatus::OK;
        return status_;
    }

    // Check for truncated file (ends abruptly without newline and last line is incomplete)
    const bool missingTrailingNewline = (contents.back() != '\n');

    std::istringstream in(contents);
    std::string line;
    int lineNo = 0;
    std::vector<domain::Account> parsed;
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
                error_ = where(path_, lineNo, verErr);
                std::string quarantine;
                quarantineFile(path_, quarantine);
                status_ = LoadStatus(LoadErrorCode::MALFORMED, error_, lineNo, path_, quarantine);
                return status_;
            }
            if (version > format::kAccountVersion) {
                error_ = where(path_, lineNo, "Unsupported version " + std::to_string(version));
                status_ = LoadStatus(LoadErrorCode::UNSUPPORTED_VERSION, error_, lineNo, path_);
                return status_;
            }
            continue;
        }
        checkedHeader = true;

        domain::Account a;
        auto parseStatus = format::deserializeAccount(line, a, lineNo);
        if (!parseStatus.isOk()) {
            if (missingTrailingNewline && in.eof()) {
                error_ = where(path_, lineNo, "Truncated record at end of file: " + parseStatus.errorMessage);
            } else {
                error_ = where(path_, lineNo, parseStatus.errorMessage);
            }
            std::string quarantine;
            quarantineFile(path_, quarantine);
            status_ = LoadStatus(LoadErrorCode::MALFORMED, error_, lineNo, path_, quarantine);
            return status_;
        }

        for (const auto& existing : parsed) {
            if (existing.accountNo == a.accountNo) {
                error_ = where(path_, lineNo, "duplicate account number " + a.accountNo);
                std::string quarantine;
                quarantineFile(path_, quarantine);
                status_ = LoadStatus(LoadErrorCode::MALFORMED, error_, lineNo, path_, quarantine);
                return status_;
            }
        }

        parsed.push_back(a);
    }

    records_ = std::move(parsed);
    status_ = LoadStatus::OK;
    return status_;
}

bool AccountRepository::save() const {
    std::string out = format::makeVersionHeader(format::kAccountVersion) + "\n";

    for (const auto& a : records_) {
        std::string line;
        std::string err;
        if (!format::serializeAccount(a, line, err)) {
            return false;
        }
        out += line + "\n";
    }

    return atomicWrite(path_, out);
}

const domain::Account* AccountRepository::find(
    const std::string& accountNo) const {
    for (const auto& a : records_) {
        if (a.accountNo == accountNo) return &a;
    }
    return nullptr;
}

domain::Account* AccountRepository::find(
    const std::string& accountNo) {
    for (auto& a : records_) {
        if (a.accountNo == accountNo) return &a;
    }
    return nullptr;
}

const domain::Account* AccountRepository::findByCustomer(
    const std::string& customerId) const {
    for (const auto& a : records_) {
        if (a.customerId == customerId) return &a;
    }
    return nullptr;
}

void AccountRepository::upsert(
    const domain::Account& account) {
    for (auto& a : records_) {
        if (a.accountNo == account.accountNo) {
            a = account;
            return;
        }
    }

    records_.push_back(account);
}

} // namespace bms::infra
