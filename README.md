# Bank Management System (Team 11)

A console-based **Bank Management System** developed in C++17 as part of the Software Engineering mini project. The application simulates essential banking operations, customer and administrator authentication, account management, transaction processing, and access control using local file storage.

The project follows a structured software engineering workflow, including requirements specification, implementation, and unit testing.

**Repository:** [Team-11-SE-PROJECT/Bank_Management_System](https://github.com/Team-11-SE-PROJECT/Bank_Management_System)

---

## Table of Contents

- [Overview](#overview)
- [Features](#features)
- [User Roles](#user-roles)
- [Security and Validation](#security-and-validation)
- [Transaction Management](#transaction-management)
- [Technology Stack](#technology-stack)
- [Prerequisites](#prerequisites)
- [Build and Run](#build-and-run)
- [Getting Started](#getting-started)
- [Testing](#testing)
- [Data Storage](#data-storage)
- [Project Documentation](#project-documentation)
- [Team](#team)

---

## Overview

The Bank Management System provides a menu-driven interface for customers and administrators to perform banking and account-management operations.

The system separates customer and administrator privileges, validates user input, maintains account information, and records financial transactions. It also incorporates authentication controls to reduce unauthorized access and protect account operations.

The application is designed as a lightweight banking simulation using local file storage rather than an external database.

## Features

### 1. Authentication and Session Management

- Customer and administrator login.
- Role-specific access to application functionality.
- Login credential validation.
- Handling of invalid login attempts.
- Automatic account locking after three failed login attempts.
- Administrator functionality to unlock locked customer accounts.
- Secure password/PIN storage.
- Credential changes for authenticated users.
- Logout and session management.

### 2. Customer and Account Management

- Customer registration and bank account creation.
- Generation of unique account numbers.
- Viewing customer and account details.
- Validation of account information and ownership.
- Separation of customer and administrator operations.

New customer accounts can be created through the administrator interface. The application generates a customer login ID and bank account number for the newly registered customer.

### 3. Banking Operations

Authenticated customers can perform the following operations:

| Operation | Description |
|---|---|
| Balance Inquiry | View the available account balance. |
| Deposit | Deposit money into an account. |
| Withdrawal | Withdraw money subject to validation and withdrawal restrictions. |
| Transaction Recording | Record deposit and withdrawal transactions. |
| Account Details | View relevant customer and bank account information. |
| Change PIN | Update the customer's login credentials. |

### 4. Deposit Validation

The deposit workflow incorporates validation to help prevent invalid transactions.

- Reject invalid deposit amounts.
- Validate deposit input before processing.
- Update account balances through the deposit operation.
- Record completed deposit transactions.

### 5. Withdrawal Validation and Limits

The withdrawal workflow incorporates financial validation and transaction restrictions.

- Reject invalid withdrawal amounts.
- Prevent withdrawals exceeding the available balance.
- Enforce configured withdrawal limits.
- Update the account balance after a valid withdrawal.
- Record completed withdrawal transactions.

These checks help maintain consistency between account balances and financial transactions.

### 6. Administrator Dashboard and Controls

Administrators have access to functionality for managing customer accounts and monitoring the application.

- Create customer accounts.
- Unlock customer accounts following repeated failed login attempts.
- View system status information.
- Change administrator credentials.
- Access administrator-only functionality.

Administrative operations are separated from customer operations through role-based access control.

---

## User Roles

The system provides two primary user roles.

### Customer

Customers can:
- Log in using their assigned credentials.
- Check their account balance.
- Deposit money.
- Withdraw money within the applicable restrictions.
- View their account details.
- Change their PIN.
- Log out of their session.

### Administrator

Administrators can:
- Authenticate using administrator credentials.
- Register customers and create bank accounts.
- Unlock accounts locked after failed login attempts.
- Display system status information.
- Change administrator credentials.
- Log out of their session.

Administrative privileges are intended to prevent customers from accessing restricted account-management operations.

---

## Security and Validation

Security and input validation are integral parts of the system.

The application implements the following controls:

- **Authentication:** Validates login credentials before granting access.
- **Role-Based Access Control:** Restricts operations according to the authenticated user's role.
- **Failed Login Protection:** Locks accounts after three consecutive failed login attempts.
- **Credential Protection:** Uses secure password/PIN storage rather than relying on plaintext credential storage.
- **Account Ownership Validation:** Validates account data and ownership during relevant operations.
- **Transaction Validation:** Checks deposit and withdrawal inputs before processing transactions.
- **Balance Protection:** Prevents withdrawals that exceed available funds.
- **Withdrawal Restrictions:** Enforces configured withdrawal limits.
- **Session Management:** Provides logout functionality and manages authenticated sessions.

These measures are intended to improve the reliability and security of the banking simulation. They do not, by themselves, establish that the application meets production banking security requirements.

---

## Technology Stack

| Component | Technology |
|---|---|
| Programming Language | C++ |
| Language Standard | C++17 |
| Application Interface | Command-line / console |
| Data Persistence | Local file storage |
| Testing | Unit tests for core services |
| Build Environment | C++17-compatible compiler and build tools |

The application is designed to run locally and does not require a remote banking server or an external database for its basic operation.

---

## Prerequisites

Before building the project, ensure that you have:

- A C++ compiler with C++17 support, such as GCC or Clang.
- Git to clone the repository.
- A terminal or command-line environment.
- The project's required build and test dependencies, if applicable.

On Ubuntu or WSL, you can install the basic development tools with:

```bash
sudo apt update
sudo apt install build-essential git
```

Verify that the compiler is available:

```bash
g++ --version
```

---

## Build and Run

### 1. Clone the Repository

```bash
git clone https://github.com/Team-11-SE-PROJECT/Bank_Management_System.git
cd Bank_Management_System
```

### 2. Build the Application

The project targets C++17. The exact build command depends on the source-file organization and build configuration in the repository.

If the project consists of a single entry-point source file and supporting implementation files, compile the required sources with:

```bash
g++ -std=c++17 -Wall -Wextra -pedantic *.cpp -o bank_management_system
```

**Note:** This command is suitable only if all `.cpp` files in the project can be compiled together into one executable. If the repository uses separate test files, multiple entry points, or a dedicated build configuration, use the appropriate build command instead.

### 3. Run the Application

On Linux or WSL:

```bash
./bank_management_system
```

On Windows, if compiled to an executable:

```powershell
.\bank_management_system.exe
```

The application opens a console-based menu through which users can authenticate and access the functionality available to their role.

---

## Getting Started

### Administrator Login

The application provides seeded administrator credentials for initial access:

| Field | Value |
|---|---|
| Login ID | `admin` |
| Password | `admin123` |

These credentials are intended for local development and demonstration.

**Security note:** Change the default administrator credentials before sharing or demonstrating the application in an environment where unauthorized access is possible. Do not reuse these credentials for real accounts.

### Creating a Customer Account

1. Start the application.
2. Log in using the administrator credentials.
3. Select the customer registration or account creation option.
4. Enter the required customer information and initial PIN.
5. Complete the account creation process.
6. Note the generated customer login ID and bank account number.
7. Log out of the administrator session.
8. Log in using the newly generated customer login ID and the initial PIN entered during registration.

The generated login ID and account number are provided by the application when the customer account is created.

**Important:** Seeded customer IDs are not automatically linked to a bank account. Create a customer account through the administrator interface to obtain a customer login ID associated with a newly created account.

### Performing Banking Operations

After customer authentication, use the customer menu to access balance inquiry, deposits, withdrawals, account details, and credential changes.

Invalid transaction inputs and withdrawals that violate the applicable balance or withdrawal restrictions should be rejected by the system.

Always log out after completing your session.

---

## Testing

Unit tests have been added for core services as part of the project's testing workflow.

The implementation and validation work covers the following functional areas:

| Area | Testing Focus |
|---|---|
| Authentication | Valid and invalid login credentials |
| Login Protection | Failed login attempts and account locking |
| Session Management | Logout and credential changes |
| Access Control | Restrictions on role-specific operations |
| Account Management | Customer registration and account creation |
| Account Validation | Account data and ownership checks |
| Deposits | Deposit amount validation and balance updates |
| Withdrawals | Amount validation and available-balance checks |
| Withdrawal Limits | Enforcement of configured restrictions |
| Transactions | Recording deposits and withdrawals |
| Administration | Account unlocking and system status |
| Core Services | Unit tests for service-level functionality |

### Running Tests

Use the test instructions provided by the repository's build configuration or test documentation.

The exact test command depends on how the unit-test executable and its dependencies are configured.

When extending the project, tests should cover successful operations, invalid inputs, boundary conditions, and failure scenarios.

---

## Data Storage

The application uses local file storage to persist relevant banking information.

This approach allows the application to retain data locally without requiring a dedicated database server.

Depending on the implemented persistence logic, stored information may include:

- Customer records.
- Account information and balances.
- Authentication-related information.
- Transaction records.
- Account-locking or system-state information.

The precise files and persistence behavior depend on the application's implementation.

**Data safety:** Before deleting or modifying application data files, make a backup if you need to preserve existing customer records or transaction history. Use only synthetic data for demonstrations.

---

## Project Documentation

The project is supported by software engineering documentation covering its requirements and testing approach.

Relevant documents include:

- **Software Requirements Specification (SRS):** Defines the functional and non-functional requirements of the system.
- **Software Test Plan:** Describes the intended testing strategy and validation activities.
- **Unit Tests:** Support the verification of core service functionality.
- **GitHub Issues:** Track implementation tasks, enhancements, and completed work.

Refer to the repository for the latest source code, implementation details, documentation, and development history.

---

## Team

**Team 11 — PES University**
**Ianna Elizabeth Reni - PES2UG24CS636**
**Pranav Reddy Melachervu  - PES2UG24CS674**
**Vikas N - PES2UG24CS668**
**Pratheek P Shettigar - PES2UG24CS650**
Software Engineering Mini Project

Developed as an academic project to demonstrate object-oriented programming, authentication, role-based access control, transaction validation, local data persistence, and software testing in C++17.

*This project is an educational software development. It is not intended for processing real financial transactions or storing real banking credentials.*