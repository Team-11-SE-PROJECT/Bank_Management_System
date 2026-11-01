#include "service/AuditLogReviewService.h"

#include <fstream>
#include <utility>

#include "infra/CredentialStore.h"
#include "service/RbacGuard.h"

namespace bms::service {

namespace {

bool contains(const std::string& value, const std::string& filter) {
    return filter.empty() || value.find(filter) != std::string::npos;
}

bool matches(
    const AuditLogRecord& record,
    const AuditLogFilter& filter
) {
    const bool dateMatches =
        filter.date.empty() ||
        record.timestamp.rfind(filter.date, 0) == 0;

    return dateMatches &&
           contains(record.event, filter.event) &&
           contains(record.account, filter.account) &&
           contains(record.result, filter.result);
}

bool parseRecord(const std::string& line, AuditLogRecord& record) {
    const std::size_t first = line.find('|');
    const std::size_t second = line.find('|', first + 1);
    const std::size_t third = line.find('|', second + 1);
    const std::size_t fourth = line.find('|', third + 1);

    if (first == std::string::npos ||
        second == std::string::npos ||
        third == std::string::npos ||
        fourth == std::string::npos) {
        return false;
    }

    record.timestamp = line.substr(0, first);
    record.event = line.substr(first + 1, second - first - 1);
    record.account = line.substr(second + 1, third - second - 1);
    record.result = line.substr(third + 1, fourth - third - 1);
    record.details = line.substr(fourth + 1);
    return true;
}

} // namespace

AuditLogReviewService::AuditLogReviewService(std::string path)
    : path_(std::move(path)) {}

domain::Status AuditLogReviewService::review(
    const SessionManager& session,
    const AuditLogFilter& filter,
    std::vector<AuditLogRecord>& outRecords
) const {
    outRecords.clear();

    if (RbacGuard::checkRole(session, infra::Role::ADMIN) !=
        domain::Status::OK) {
        return domain::Status::UNAUTHORIZED;
    }

    std::ifstream input(path_);
    if (!input) {
        return domain::Status::STORAGE_FAILURE;
    }

    std::string line;
    while (std::getline(input, line)) {
        AuditLogRecord record;
        if (!parseRecord(line, record)) {
            continue;
        }
        if (matches(record, filter)) {
            outRecords.push_back(std::move(record));
        }
    }

    return input.bad()
        ? domain::Status::STORAGE_FAILURE
        : domain::Status::OK;
}

} // namespace bms::service
