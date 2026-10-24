#include "infra/AuditLogger.h"

#include <fstream>
#include <utility>
#include "infra/TimeUtil.h"

namespace bms::infra {

AuditLogger::AuditLogger(std::string path)
    : path_(std::move(path)) {}

bool AuditLogger::log(
    const std::string& event,
    const std::string& account,
    const std::string& result,
    const std::string& details
) const {
    std::ofstream out(path_, std::ios::app);

    if (!out) {
        return false;
    }

    out << nowTimestamp() << '|'
        << event << '|'
        << account << '|'
        << result << '|'
        << details << '\n';

    out.flush();
    return static_cast<bool>(out);
}

} // namespace bms::infra