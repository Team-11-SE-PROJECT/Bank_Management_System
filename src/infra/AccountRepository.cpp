#include "infra/AccountRepository.h"

#include <sstream>
#include <utility>

#include "infra/DataFormat.h"
#include "infra/FileUtil.h"

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

    std::string contents;
    if (!readAll(path_, contents)) {
        return LoadStatus::MISSING;
    }

    std::istringstream in(contents);
    std::string line;
    int lineNo = 0;
    std::vector<domain::Account> parsed;

    while (std::getline(in, line)) {
        ++lineNo;

        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (line.empty()) continue;

        const auto f = format::split(line);

        if (f.size() != format::kAccountFields) {
            error_ = where(
                path_, lineNo,
                "expected 6 fields but found " +
                std::to_string(f.size()));
            return LoadStatus::CORRUPT;
        }

        domain::Account a;
        a.accountNo = f[0];
        a.customerId = f[1];

        if (a.accountNo.empty() || a.customerId.empty()) {
            error_ = where(
                path_, lineNo,
                "account number and customer id must not be empty");
            return LoadStatus::CORRUPT;
        }

        if (!format::parseNonNegative(f[2], a.balancePaise)) {
            error_ = where(
                path_, lineNo,
                "balance must be a non-negative whole number of paise");
            return LoadStatus::CORRUPT;
        }

        if (f[3] != "ACTIVE" && f[3] != "CLOSED") {
        error_ = where(
          path_, lineNo,
          "status must be ACTIVE or CLOSED");
        return LoadStatus::CORRUPT;
    }

    a.status = f[3];

        if (!format::parseNonNegative(
                f[4], a.withdrawnTodayPaise)) {
            error_ = where(
                path_, lineNo,
                "withdrawnToday must be a non-negative whole number");
            return LoadStatus::CORRUPT;
        }

        if (f[5] != "-" && !format::isDate(f[5])) {
            error_ = where(
                path_, lineNo,
                "lastWithdrawDate must be YYYY-MM-DD or -");
            return LoadStatus::CORRUPT;
        }

        a.lastWithdrawDate = f[5];

        for (const auto& existing : parsed) {
            if (existing.accountNo == a.accountNo) {
                error_ = where(
                    path_, lineNo,
                    "duplicate account number " + a.accountNo);
                return LoadStatus::CORRUPT;
            }
        }

        parsed.push_back(a);
    }

    records_ = std::move(parsed);
    return LoadStatus::OK;
}

bool AccountRepository::save() const {
    std::string out;

    for (const auto& a : records_) {
        out += format::join({
            a.accountNo,
            a.customerId,
            std::to_string(a.balancePaise),
            a.status,
            std::to_string(a.withdrawnTodayPaise),
            a.lastWithdrawDate.empty() ? "-" : a.lastWithdrawDate
        });

        out += '\n';
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