
#include "service/RbacGuard.h"
#include "TestHarness.h"

#include <thread>
#include <chrono>

using namespace bms::service;
using namespace bms::infra;
using namespace bms::domain;

static void test_rbac_guard() {
    SessionManager sm;

    // No active session: deny both roles.
    CHECK(RbacGuard::checkRole(sm, Role::ADMIN) == Status::UNAUTHORIZED);
    CHECK(RbacGuard::checkRole(sm, Role::CUSTOMER) == Status::UNAUTHORIZED);

    // Customer can access customer-only operations, not admin operations.
    sm.login("cust1001", Role::CUSTOMER, 300);
    CHECK(RbacGuard::checkRole(sm, Role::CUSTOMER) == Status::OK);
    CHECK(RbacGuard::checkRole(sm, Role::ADMIN) == Status::UNAUTHORIZED);

    // Administrator can access admin-only operations.
    sm.login("admin", Role::ADMIN, 300);
    CHECK(RbacGuard::checkRole(sm, Role::ADMIN) == Status::OK);
    CHECK(RbacGuard::checkRole(sm, Role::CUSTOMER) == Status::UNAUTHORIZED);

    // Logout must revoke access.
    sm.logout();
    CHECK(RbacGuard::checkRole(sm, Role::ADMIN) == Status::UNAUTHORIZED);
    CHECK(RbacGuard::checkRole(sm, Role::CUSTOMER) == Status::UNAUTHORIZED);

    // An expired administrator session must be denied.
    sm.login("admin", Role::ADMIN, 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    CHECK(RbacGuard::checkRole(sm, Role::ADMIN) == Status::UNAUTHORIZED);
}

int main() {
    test_rbac_guard();
    return bms::test::summary("test_rbac");
}
