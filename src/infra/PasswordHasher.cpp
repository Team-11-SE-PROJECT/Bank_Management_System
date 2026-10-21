#include "infra/PasswordHasher.h"

#include <cstring>
#include <random>
#include <vector>

namespace bms::infra {

namespace {
constexpr std::uint32_t K[64] = {
    0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,
    0x923f82a4u,0xab1c5ed5u,0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,
    0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,0xe49b69c1u,0xefbe4786u,
    0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
    0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,
    0x06ca6351u,0x14292967u,0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,
    0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,0xa2bfe8a1u,0xa81a664bu,
    0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
    0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,
    0x5b9cca4fu,0x682e6ff3u,0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,
    0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
};

inline std::uint32_t rotr(std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

void sha256(const std::uint8_t* data, std::size_t len, std::uint8_t out[32]) {
    std::uint32_t h[8] = {
        0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,
        0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u
    };

    const std::size_t bitLenHi = static_cast<std::uint32_t>((len * 8ULL) >> 32);
    const std::size_t bitLenLo = static_cast<std::uint32_t>(len * 8ULL);

    std::size_t padded = ((len + 8) / 64 + 1) * 64;
    std::vector<std::uint8_t> buf(padded, 0);
    std::memcpy(buf.data(), data, len);
    buf[len] = 0x80;
    for (int i = 0; i < 8; ++i)
        buf[padded - 1 - i] = static_cast<std::uint8_t>((bitLenLo >> (8 * i)) & 0xff);
    for (int i = 0; i < 4; ++i)
        buf[padded - 9 - i] = static_cast<std::uint8_t>((bitLenHi >> (8 * i)) & 0xff);

    for (std::size_t off = 0; off < padded; off += 64) {
        std::uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<std::uint32_t>(buf[off + i*4    ]) << 24)
                 | (static_cast<std::uint32_t>(buf[off + i*4 + 1]) << 16)
                 | (static_cast<std::uint32_t>(buf[off + i*4 + 2]) << 8)
                 |  static_cast<std::uint32_t>(buf[off + i*4 + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            std::uint32_t s0 = rotr(w[i-15],7) ^ rotr(w[i-15],18) ^ (w[i-15] >> 3);
            std::uint32_t s1 = rotr(w[i-2],17) ^ rotr(w[i-2],19)  ^ (w[i-2] >> 10);
            w[i] = w[i-16] + s0 + w[i-7] + s1;
        }
        std::uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
        for (int i = 0; i < 64; ++i) {
            std::uint32_t S1 = rotr(e,6) ^ rotr(e,11) ^ rotr(e,25);
            std::uint32_t ch = (e & f) ^ (~e & g);
            std::uint32_t t1 = hh + S1 + ch + K[i] + w[i];
            std::uint32_t S0 = rotr(a,2) ^ rotr(a,13) ^ rotr(a,22);
            std::uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
            std::uint32_t t2 = S0 + mj;
            hh=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
        }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
    }
    for (int i = 0; i < 8; ++i) {
        out[i*4    ] = static_cast<std::uint8_t>((h[i] >> 24) & 0xff);
        out[i*4 + 1] = static_cast<std::uint8_t>((h[i] >> 16) & 0xff);
        out[i*4 + 2] = static_cast<std::uint8_t>((h[i] >>  8) & 0xff);
        out[i*4 + 3] = static_cast<std::uint8_t>((h[i]      ) & 0xff);
    }
}
} // anonymous namespace

std::array<std::uint8_t, PasswordHasher::kSaltBytes> PasswordHasher::generateSalt() {
    std::array<std::uint8_t, kSaltBytes> salt{};
    std::random_device rd;
    for (auto& b : salt) b = static_cast<std::uint8_t>(rd() & 0xff);
    return salt;
}

std::string PasswordHasher::hash(const std::string& secret,
                                 const std::array<std::uint8_t, kSaltBytes>& salt) {
    std::string input;
    input.reserve(kSaltBytes + secret.size());
    input.append(reinterpret_cast<const char*>(salt.data()), kSaltBytes);
    input.append(secret);

    std::uint8_t digest[32];
    sha256(reinterpret_cast<const std::uint8_t*>(input.data()), input.size(), digest);
    return toHex(digest, sizeof(digest));
}

bool PasswordHasher::verify(const std::string& secret,
                            const std::array<std::uint8_t, kSaltBytes>& salt,
                            const std::string& expectedHexHash) {
    const std::string actual = hash(secret, salt);
    if (actual.size() != expectedHexHash.size()) return false;
    unsigned diff = 0;
    for (std::size_t i = 0; i < actual.size(); ++i)
        diff |= static_cast<unsigned>(actual[i] ^ expectedHexHash[i]);
    return diff == 0;
}

std::string PasswordHasher::toHex(const std::uint8_t* data, std::size_t len) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (std::size_t i = 0; i < len; ++i) {
        out.push_back(digits[(data[i] >> 4) & 0xf]);
        out.push_back(digits[ data[i]       & 0xf]);
    }
    return out;
}

bool PasswordHasher::fromHex(const std::string& hex, std::uint8_t* out, std::size_t outLen) {
    if (hex.size() != outLen * 2) return false;
    auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (std::size_t i = 0; i < outLen; ++i) {
        int hi = nib(hex[i*2]), lo = nib(hex[i*2 + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = static_cast<std::uint8_t>((hi << 4) | lo);
    }
    return true;
}

} // namespace bms::infra