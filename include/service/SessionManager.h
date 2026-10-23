#pragma once
#include <chrono>
#include <string>
#include "infra/CredentialStore.h" // for bms::infra::Role
namespace bms::service {
class SessionManager {
public:
    // Sets the current logged-in user and resets the inactivity timer
    void login(const std::string& userId, infra::Role role, int timeoutSeconds);
    // Clears the current user (Logout)
    void logout();
    // Returns true if someone is logged in and the timer hasn't expired
    bool isActive() const;
    // Call this whenever the user performs an action (resets the timer)
    void refreshActivity();
    // Getters
    std::string getUserId() const;
    infra::Role getRole() const;

private:
    std::string currentUser_;
    infra::Role currentRole_ = infra::Role::CUSTOMER;
    bool active_ = false;

    int timeoutSeconds_ = 300; // Default 5 minutes
    std::chrono::steady_clock::time_point lastActivity_;
};

} // namespace bms::service