#include "service/RbacGuard.h"
#include "TestHarness.h"

using namespace bms::service;
using namespace bms::infra;
using namespace bms::domain;

static void test_rbac_guard() {
    SessionManager sm;
    
    // 1. No one is logged in -> Should be unauthorized
    CHECK(RbacGuard::checkRole(sm, Role::ADMIN) == Status::UNAUTHORIZED);
    CHECK(RbacGuard::checkRole(sm, Role::CUSTOMER) == Status::UNAUTHORIZED);

    // 2. Login as a CUSTOMER
    sm.login("cust1001", Role::CUSTOMER, 300);
    CHECK(RbacGuard::checkRole(sm, Role::CUSTOMER) == Status::OK); // Allowed
    CHECK(RbacGuard::checkRole(sm, Role::ADMIN) == Status::UNAUTHORIZED); // Denied

    // 3. Login as an ADMIN
    sm.login("admin", Role::ADMIN, 300);
    CHECK(RbacGuard::checkRole(sm, Role::ADMIN) == Status::OK); // Allowed
    CHECK(RbacGuard::checkRole(sm, Role::CUSTOMER) == Status::UNAUTHORIZED); // Denied

    // 4. Logout -> Should deny everything
    sm.logout();
    CHECK(RbacGuard::checkRole(sm, Role::ADMIN) == Status::UNAUTHORIZED);
    CHECK(RbacGuard::checkRole(sm, Role::CUSTOMER) == Status::UNAUTHORIZED);
}

int main() {
    test_rbac_guard();
    return bms::test::summary("test_rbac");
}