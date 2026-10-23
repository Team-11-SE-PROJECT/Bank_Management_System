#include "service/SessionManager.h"
#include "TestHarness.h"
#include <thread>
#include <chrono>
using namespace bms::service;
using namespace bms::infra;
static void test_login_logout() {
    SessionManager sm;
    CHECK(!sm.isActive());
    sm.login("cust1001", Role::CUSTOMER, 300);
    CHECK(sm.isActive());
    CHECK_EQ(sm.getUserId(), std::string("cust1001"));
    CHECK(sm.getRole() == Role::CUSTOMER);
    sm.logout();
    CHECK(!sm.isActive());
}

static void test_timeout() {
    SessionManager sm;
    // Login with a 1-second timeout
    sm.login("admin", Role::ADMIN, 1);
    CHECK(sm.isActive());

    // Sleep for 2 seconds to trigger the timeout
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    CHECK(!sm.isActive()); // Should be expired now
}

int main() {
    test_login_logout();
    test_timeout();
    return bms::test::summary("test_session");
}