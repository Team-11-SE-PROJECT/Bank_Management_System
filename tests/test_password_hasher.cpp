#include "infra/PasswordHasher.h"
#include "infra/FileUtil.h"
#include "TestHarness.h"

#include <cstdio>
#include <filesystem>
#include <string>

using namespace bms::infra;

static void test_same_pin_diff_salt() {
    auto s1 = PasswordHasher::generateSalt();
    auto s2 = PasswordHasher::generateSalt();
    const std::string pin = "1234";
    CHECK(PasswordHasher::hash(pin, s1) != PasswordHasher::hash(pin, s2));
}

static void test_verify_roundtrip() {
    auto s = PasswordHasher::generateSalt();
    const std::string pin = "4321";
    const std::string h = PasswordHasher::hash(pin, s);
    CHECK(PasswordHasher::verify(pin, s, h));
    CHECK(!PasswordHasher::verify("0000", s, h));
}

static void test_hex_roundtrip() {
    std::uint8_t buf[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    std::string hex = PasswordHasher::toHex(buf, 4);
    CHECK_EQ(hex, std::string("deadbeef"));
    std::uint8_t out[4];
    CHECK(PasswordHasher::fromHex(hex, out, 4));
    CHECK(out[0] == 0xDE && out[1] == 0xAD && out[2] == 0xBE && out[3] == 0xEF);
}

static void test_atomic_write() {
    const std::string path = "test_atomic.dat";
    CHECK(atomicWrite(path, "hello"));
    std::string got;
    CHECK(readAll(path, got));
    CHECK_EQ(got, std::string("hello"));
    CHECK(atomicWrite(path, "world"));
    CHECK(readAll(path, got));
    CHECK_EQ(got, std::string("world"));
    std::filesystem::remove(path);
}

int main() {
    test_same_pin_diff_salt();
    test_verify_roundtrip();
    test_hex_roundtrip();
    test_atomic_write();
    return bms::test::summary("test_password_hasher");
}