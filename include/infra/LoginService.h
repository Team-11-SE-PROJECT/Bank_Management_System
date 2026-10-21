#pragma once

#include "infra/CredentialStore.h"

namespace bms::infra {

enum class LoginStatus {
    SUCCESS,
    INVALID_CREDENTIALS,
    ACCOUNT_LOCKED,
    STORAGE_ERROR
};

struct LoginResult {
    LoginStatus status;
    int attemptsRemaining = 0;
};

class LoginService {
public:
    static constexpr int kMaxFailedAttempts = 3;

    explicit LoginService(CredentialStore& store) : store_(store) {}

    LoginResult authenticate(const std::string& userId,
                             const std::string& password,
                             Role expectedRole);

private:
    CredentialStore& store_;
};

} // namespace bms::infra