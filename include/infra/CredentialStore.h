#pragma once
#include <array>
#include <string>
#include <vector>
#include "infra/PasswordHasher.h"
namespace bms::infra {
enum class Role { CUSTOMER, ADMIN };
struct Credential {
    std::string userId;
    Role role = Role::CUSTOMER;
    std::array<std::uint8_t, PasswordHasher::kSaltBytes> salt{};
    std::string hashHex;
    int failedAttempts = 0;
    bool locked = false;
};

class CredentialStore {
public:
    explicit CredentialStore(std::string path);
    bool load();
    bool save() const;
    const Credential* find(const std::string& userId) const;
    Credential*       find(const std::string& userId);
    void upsert(const Credential& cred);
    const std::vector<Credential>& all() const { return records_; }
    static const char* roleToString(Role r);
    static bool        roleFromString(const std::string& s, Role& out);
private:
    std::string path_;
    std::vector<Credential> records_;
};

} // namespace bms::infra