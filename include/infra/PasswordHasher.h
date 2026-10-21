#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace bms::infra {

// 16-byte random salt + SHA-256(salt || secret) stored as hex strings.
class PasswordHasher {
public:
    static constexpr std::size_t kSaltBytes = 16;

    // Generates a cryptographically-random salt.
    static std::array<std::uint8_t, kSaltBytes> generateSalt();

    // SHA-256(salt || secret) -> lowercase hex.
    static std::string hash(const std::string& secret,
                            const std::array<std::uint8_t, kSaltBytes>& salt);

    // Does `secret` match `expectedHexHash` given this salt?
    static bool verify(const std::string& secret,
                       const std::array<std::uint8_t, kSaltBytes>& salt,
                       const std::string& expectedHexHash);

    // Hex helpers (used by CredentialStore).
    static std::string toHex(const std::uint8_t* data, std::size_t len);
    static bool fromHex(const std::string& hex,
                        std::uint8_t* out, std::size_t outLen);
};

} // namespace bms::infra