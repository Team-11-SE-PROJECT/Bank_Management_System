
# Bank Management System (Team 11)

A console-based Bank Management System written in C++17 for the Software Engineering mini project
(PES University). It simulates banking operations with local file storage.
See the SRS (Deliverable 1) and Software Test Plan for the full requirements.

## Features

After customer login, the menu provides balance inquiry, cash/cheque deposits,
withdrawals, account details, and PIN changes. After admin login, the menu
provides customer account creation, customer-login unlock, system status, and
admin PIN changes. Login also enforces role-specific credentials and locks an
account after repeated failed attempts.

Use the admin credentials `admin` / `admin123` from the seed data to create a
customer account. The app prints that customer's generated login ID and account
number; use the initial PIN entered during account creation to log in as that
customer. Seeded customer IDs are not automatically linked to a bank account.
