#include "service/SessionManager.h"

namespace bms::service {

void SessionManager::login(const std::string& userId, infra::Role role, int timeoutSeconds) {
    currentUser_ = userId;
    currentRole_ = role;
    timeoutSeconds_ = timeoutSeconds;
    active_ = true;
    refreshActivity();
}

void SessionManager::logout() {
    currentUser_.clear();
    active_ = false;
}

bool SessionManager::isActive() const {
    if (!active_) return false;

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastActivity_).count();
    
    if (elapsed >= timeoutSeconds_) {
        return false; // Session expired
    }
    return true;
}

void SessionManager::refreshActivity() {
    lastActivity_ = std::chrono::steady_clock::now();
}

std::string SessionManager::getUserId() const {
    return currentUser_;
}

infra::Role SessionManager::getRole() const {
    return currentRole_;
}

} // namespace bms::service