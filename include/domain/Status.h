#pragma once
namespace bms::domain {
enum class Status {
    OK,
    UNAUTHORIZED,
    INVALID_INPUT,
    NOT_FOUND,
    STORAGE_FAILURE
};
} // namespace bms::domain