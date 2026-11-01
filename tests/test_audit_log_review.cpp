#include "service/AuditLogReviewService.h"
#include "infra/AuditLogger.h"
#include "TestHarness.h"
#include "TestUtil.h"

#include <vector>

int main() {
    bms::test::TempDir dir;
    const std::string path = dir.file("audit.log");
    bms::infra::AuditLogger logger(path);
    CHECK(logger.log("DEPOSIT", "BMS1001", "SUCCESS", "amountPaise=500"));
    CHECK(logger.log("WITHDRAWAL", "BMS1001", "FAILED_INSUFFICIENT_FUNDS", "amountPaise=999"));
    CHECK(logger.log("LOGIN", "customer1", "SUCCESS"));

    bms::service::AuditLogReviewService review(path);
    bms::service::AuditLogFilter filter;
    std::vector<bms::service::AuditLogRecord> records;
    bms::service::SessionManager customer;
    customer.login("customer1", bms::infra::Role::CUSTOMER, 300);
    CHECK(review.review(customer, filter, records) == bms::domain::Status::UNAUTHORIZED);
    CHECK(records.empty());

    bms::service::SessionManager admin;
    admin.login("admin", bms::infra::Role::ADMIN, 300);
    CHECK(review.review(admin, filter, records) == bms::domain::Status::OK);
    CHECK(records.size() == 3u);

    filter.event = "WITHDRAWAL";
    filter.result = "FAILED";
    CHECK(review.review(admin, filter, records) == bms::domain::Status::OK);
    CHECK(records.size() == 1u);
    if (!records.empty()) {
        CHECK(records[0].account == "BMS1001");
        CHECK(records[0].details == "amountPaise=999");
    }

    filter = {};
    filter.account = "unknown";
    CHECK(review.review(admin, filter, records) == bms::domain::Status::OK);
    CHECK(records.empty());

    return bms::test::summary("test_audit_log_review");
}
