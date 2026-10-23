// Issue #26: amount validation (BMS-SR-004).
#include "infra/InputValidator.h"
#include "TestHarness.h"

using bms::infra::InputValidator;
using bms::infra::kMaxAmountPaise;

static void test_valid_amounts() {
    long long p = 0;
    CHECK(InputValidator::parseAmountPaise("500", p));     CHECK_EQ(p, 50000);
    CHECK(InputValidator::parseAmountPaise("250.75", p));  CHECK_EQ(p, 25075);
    CHECK(InputValidator::parseAmountPaise("10.5", p));    CHECK_EQ(p, 1050);
    CHECK(InputValidator::parseAmountPaise("0000000001.20", p));  CHECK_EQ(p, 120);
}
static void test_boundaries() {
    long long p = 0;
    CHECK(!InputValidator::parseAmountPaise("0", p));
    CHECK(!InputValidator::parseAmountPaise("0.00", p));
    CHECK(InputValidator::parseAmountPaise("0.01", p));            CHECK_EQ(p, 1);
    CHECK(InputValidator::parseAmountPaise("999999999.99", p));    CHECK_EQ(p, kMaxAmountPaise);
    CHECK(!InputValidator::parseAmountPaise("1000000000", p));
}
static void test_negative_and_non_numeric() {
    long long p = 0;
    const char* bad[] = {"", "-50", "-0.01", "abc", "12abc", "1e3", " 5", "5 ", ".5", "5.", "1.234", "1,000", "+5", "1.2.3"};
    for (const char* s : bad) CHECK(!InputValidator::parseAmountPaise(s, p));
}
static void test_output_untouched_on_failure() {
    long long p = 777;
    CHECK(!InputValidator::parseAmountPaise("abc", p));
    CHECK_EQ(p, 777);
}
static void test_format_paise() {
    CHECK_EQ(InputValidator::formatPaise(150050), "1500.50");
    CHECK_EQ(InputValidator::formatPaise(5), "0.05");
    CHECK_EQ(InputValidator::formatPaise(0), "0.00");
    CHECK_EQ(InputValidator::formatPaise(-5), "-0.05");
}

int main() {
    test_valid_amounts();
    test_boundaries();
    test_negative_and_non_numeric();
    test_output_untouched_on_failure();
    test_format_paise();
    return bms::test::summary("test_input_validator");
}
