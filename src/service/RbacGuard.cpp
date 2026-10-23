#include "service/RbacGuard.h"
namespace bms::service {
domain::Status RbacGuard::checkRole(const SessionManager& session, infra::Role requiredRole) {
    // 1. Check if logged in and session hasn't expired
    if (!session.isActive()) {
        return domain::Status::UNAUTHORIZED;
    }
    // 2. Check if the user has the correct role
    if (session.getRole() != requiredRole) {
        return domain::Status::UNAUTHORIZED;
    }
    return domain::Status::OK;
}
} // namespace bms::service