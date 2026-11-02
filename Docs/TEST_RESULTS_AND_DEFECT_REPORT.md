# Test Results and Defect Report

## Test run summary

| Item | Result |
|---|---|
| Execution date | 2026-11-02 |
| Platform | Windows |
| Build type | Debug |
| CMake | 4.5.0-rc1 |
| Generator | MinGW Makefiles |
| C++ compiler | MinGW-w64 GCC 14.2.0 |
| Configure | Passed |
| Build | Passed |
| Automated tests | 27 passed, 0 failed |
| Total CTest duration | 7.34 seconds |

## Reproduction

Run these commands from the repository root:

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

## Test results

| Test | Result |
|---|---|
| `test_password_hasher` | Passed |
| `test_login_service` | Passed |
| `test_change_credentials` | Passed |
| `test_session` | Passed |
| `test_rbac` | Passed |
| `test_input_validator` | Passed |
| `test_customer_account_store` | Passed |
| `test_customer_update` | Passed |
| `test_customer_update_service` | Passed |
| `test_account_service` | Passed |
| `test_account_unlock_service` | Passed |
| `test_system_status_service` | Passed |
| `test_transaction_service` | Passed |
| `test_account_number_generator` | Passed |
| `test_account_details_service` | Passed |
| `test_data_format` | Passed |
| `test_atomic_save` | Passed |
| `test_data_integrity` | Passed |
| `test_startup_loading` | Passed |
| `test_persistence` | Passed |
| `test_file_error_handling` | Passed |
| `test_journal_recovery` | Passed |
| `test_customer_search_service` | Passed |
| `test_account_closure_service` | Passed |
| `test_audit_logger` | Passed |
| `test_audit_log_review` | Passed |
| `test_transaction_history` | Passed |

### Coverage areas

The automated suite exercises password hashing, authentication and lockout,
session and role enforcement, input validation, customer/account creation and
updates, account search and closure, deposits, withdrawals and transfers,
transaction history, audit logging and review, persistence, file-error handling,
and journal recovery.

## Defect report

No defects were identified by this test run. All configured tests passed.

| Defect ID | Description | Severity | Status |
|---|---|---|---|
| — | No confirmed defects from this run. | — | — |

## Limitations

- This report records a Windows Debug build and CTest run only. Linux results
  must be confirmed by the repository's Ubuntu CI job.
- These results cover the configured automated tests; they are not a
  production-readiness, performance, or security certification.
