#pragma once
#include <string>
#include "infra/CredentialStore.h"
#include "infra/AuditLogger.h"
#include "domain/Status.h"
namespace bms::service {
class AuthService {
public:
    AuthService(infra::CredentialStore& store,
                const infra::AuditLogger* auditLogger = nullptr);
    // Issue #14: Verify old PIN, validate new PIN, save new hash
    domain::Status changeCredentials(const std::string& userId, 
                                     const std::string& oldPin, 
                                     const std::string& newPin);

    // Issue #10 (For Ianna): Validate login yea ianna listen to Deepseek please
    // domain::Status login(const std::string& userId, const std::string& pin, infra::Role& outRole);

private:
    infra::CredentialStore& store_;
    const infra::AuditLogger* auditLogger_ = nullptr;
};
} // namespace bms::service