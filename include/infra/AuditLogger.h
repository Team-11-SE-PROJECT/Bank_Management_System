#pragma once
#include <string>

namespace bms::infra {

class AuditLogger {
public:
    explicit AuditLogger(std::string path);

    bool logError(
        const std::string& event,
        const std::string& severity,
        const std::string& details = {}
    ) const;

    bool log(
        const std::string& event,
        const std::string& account,
        const std::string& result,
        const std::string& details = {}
    ) const;

private:
    std::string path_;
};

} // namespace bms::infra