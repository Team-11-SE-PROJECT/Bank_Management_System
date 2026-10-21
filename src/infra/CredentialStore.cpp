#include "infra/CredentialStore.h"

#include <sstream>

#include "infra/FileUtil.h"

namespace bms::infra {

CredentialStore::CredentialStore(std::string path) : path_(std::move(path)) {}

const char* CredentialStore::roleToString(Role r) {
    return r == Role::ADMIN ? "ADMIN" : "CUSTOMER";
}

bool CredentialStore::roleFromString(const std::string& s, Role& out) {
    if (s == "ADMIN")    { out = Role::ADMIN;    return true; }
    if (s == "CUSTOMER") { out = Role::CUSTOMER; return true; }
    return false;
}

bool CredentialStore::load() {
    records_.clear();

    std::string contents;
    if (!readAll(path_, contents)) {
        return true;
    }

    std::istringstream in(contents);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;

        std::vector<std::string> f;
        std::string tok;
        std::istringstream ls(line);
        while (std::getline(ls, tok, '|')) f.push_back(tok);
        if (f.size() != 6) return false;

        Credential c;
        c.userId = f[0];

        Role r;
        if (!roleFromString(f[1], r)) return false;
        c.role = r;

        if (!PasswordHasher::fromHex(f[2], c.salt.data(), c.salt.size())) return false;
        c.hashHex = f[3];

        try {
            c.failedAttempts = std::stoi(f[4]);
        } catch (...) {
            return false;
        }
        c.locked = (f[5] == "1");
        records_.push_back(std::move(c));
    }
    return true;
}

bool CredentialStore::save() const {
    std::string out;
    for (const auto& c : records_) {
        out += c.userId;
        out += '|';
        out += roleToString(c.role);
        out += '|';
        out += PasswordHasher::toHex(c.salt.data(), c.salt.size());
        out += '|';
        out += c.hashHex;
        out += '|';
        out += std::to_string(c.failedAttempts);
        out += '|';
        out += (c.locked ? "1" : "0");
        out += '\n';
    }
    return atomicWrite(path_, out);
}

const Credential* CredentialStore::find(const std::string& userId) const {
    for (const auto& c : records_) if (c.userId == userId) return &c;
    return nullptr;
}

Credential* CredentialStore::find(const std::string& userId) {
    for (auto& c : records_) if (c.userId == userId) return &c;
    return nullptr;
}

void CredentialStore::upsert(const Credential& cred) {
    for (auto& c : records_) {
        if (c.userId == cred.userId) { c = cred; return; }
    }
    records_.push_back(cred);
}

} // namespace bms::infra