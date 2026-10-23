#pragma once
#include "service/SessionManager.h"
#include "domain/Status.h"
namespace bms::service {
class RbacGuard {
public:
    // Checks if the current session is active AND has the required role.
    // Returns Status::OK if allowed, Status::UNAUTHORIZED if denied.
    static domain::Status checkRole(const SessionManager& session, infra::Role requiredRole);
};

} // namespace bms::service