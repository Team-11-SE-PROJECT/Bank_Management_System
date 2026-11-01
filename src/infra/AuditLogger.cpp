#include "infra/AuditLogger.h"

#include <fstream>
#include <utility>
#include "infra/TimeUtil.h"

namespace bms::infra {

namespace {
std::string safeField(std::string value) {
    for (char& ch : value) {
        if (ch == '|' || ch == '\n' || ch == '\r') ch = ' ';
    }
    return value;
}
} // namespace

AuditLogger::AuditLogger(std::string path)
    : path_(std::move(path)) {}

bool AuditLogger::log(
    const std::string& event,
    const std::string& account,
    const std::string& result,
    const std::string& details
) const {
    std::ofstream out(path_, std::ios::app);
    if (!out) return false;

    out << nowTimestamp() << '|'
        << safeField(event) << '|'
        << safeField(account) << '|'
        << safeField(result) << '|'
        << safeField(details) << '\n';
    out.flush();
    return static_cast<bool>(out);
}

bool AuditLogger::logError(
    const std::string& event,
    const std::string& severity,
    const std::string& details
) const {
    if (severity != "ERROR" && severity != "CRITICAL") return false;
    return log(event, "SYSTEM", severity, details);
}

} // namespace bms::infra
