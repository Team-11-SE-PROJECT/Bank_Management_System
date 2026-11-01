#pragma once

#include <string>
#include <vector>

#include "domain/Status.h"
#include "service/SessionManager.h"

namespace bms::service {

struct AuditLogRecord {
    std::string timestamp;
    std::string event;
    std::string account;
    std::string result;
    std::string details;
};

struct AuditLogFilter {
    std::string date;
    std::string event;
    std::string account;
    std::string result;
};

class AuditLogReviewService {
public:
    explicit AuditLogReviewService(std::string path);

    domain::Status review(
        const SessionManager& session,
        const AuditLogFilter& filter,
        std::vector<AuditLogRecord>& outRecords
    ) const;

private:
    std::string path_;
};

} // namespace bms::service
