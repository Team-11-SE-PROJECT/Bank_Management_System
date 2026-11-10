#include "TestHarness.h"
#include "TestUtil.h"

#include "domain/Account.h"
#include "domain/Customer.h"
#include "infra/CustomerAccountStore.h"
#include "service/AccountDetailsService.h"

namespace {

void test_account_details_access_and_validation() {
    bms::test::TempDir dir;
    bms::infra::CustomerAccountStore store(
        dir.file("customers.dat"), dir.file("accounts.dat")
    );
    CHECK(store.load());

    const bms::domain::Customer customer{
        "CUST0001", "Test Customer", "9876543210", "Bengaluru"
    };
    CHECK(store.addCustomer(customer));

    bms::domain::Account account;
    account.accountNo = "BMS000001";
    account.customerId = customer.customerId;
    account.balancePaise = 125050;
    CHECK(store.addAccount(account));

    const bms::service::AccountDetailsService service(store);

    CHECK(service.getByAccountNumber("", customer.customerId).status
          == bms::service::AccountDetailsStatus::INVALID_INPUT);
    CHECK(service.getByAccountNumber(account.accountNo, "").status
          == bms::service::AccountDetailsStatus::INVALID_INPUT);
    CHECK(service.getByAccountNumber("BMS999999", customer.customerId).status
          == bms::service::AccountDetailsStatus::NOT_FOUND);
    CHECK(service.getByAccountNumber(account.accountNo, "CUST9999").status
          == bms::service::AccountDetailsStatus::UNAUTHORIZED);

    const auto result =
        service.getByAccountNumber(account.accountNo, customer.customerId);
    CHECK(result.status == bms::service::AccountDetailsStatus::OK);
    CHECK(result.customer.customerId == customer.customerId);
    CHECK(result.customer.name == customer.name);
    CHECK(result.account.accountNo == account.accountNo);
    CHECK(result.account.balancePaise == account.balancePaise);
}

} // namespace

int main() {
    test_account_details_access_and_validation();
    return bms::test::summary("test_account_details_service");
}
