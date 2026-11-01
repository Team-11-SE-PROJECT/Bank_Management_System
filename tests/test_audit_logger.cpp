#include "infra/AuditLogger.h"
#include "TestHarness.h"
#include "TestUtil.h"

#include <string>

int main() {
    bms::test::TempDir dir;
    bms::infra::AuditLogger audit(dir.file("audit.log"));

    CHECK(audit.logError("CONFIG_LOAD", "CRITICAL", "bad config"));
    CHECK(audit.logError("FILE_SAVE", "ERROR", "disk full"));
    CHECK(!audit.logError("FILE_SAVE", "INFO", "not an error"));
    CHECK(audit.log("CUSTOM_EVENT", "user|forged", "SUCCESS", "line1\nFAKE"));

    const std::string contents = bms::test::readFile(dir.file("audit.log"));
    CHECK(contents.find("|CONFIG_LOAD|SYSTEM|CRITICAL|bad config") != std::string::npos);
    CHECK(contents.find("|FILE_SAVE|SYSTEM|ERROR|disk full") != std::string::npos);
    CHECK(contents.find("user forged") != std::string::npos);
    CHECK(contents.find("line1 FAKE") != std::string::npos);
    CHECK(contents.find("\nFAKE") == std::string::npos);

    return bms::test::summary("test_audit_logger");
}
