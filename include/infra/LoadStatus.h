#pragma once

#include <string>
#include <utility>

namespace bms::infra {

enum class LoadErrorCode {
    OK = 0,
    MISSING,
    CORRUPT,
    MALFORMED,
    UNSUPPORTED_VERSION,
    IO_ERROR
};

struct LoadStatus {
    LoadErrorCode errorCode = LoadErrorCode::OK;
    std::string errorMessage;
    int lineNumber = 0;
    std::string originalPath;
    std::string quarantinePath;

    // Backward-compatibility constants matching LoadErrorCode
    static constexpr LoadErrorCode OK = LoadErrorCode::OK;
    static constexpr LoadErrorCode MISSING = LoadErrorCode::MISSING;
    static constexpr LoadErrorCode CORRUPT = LoadErrorCode::CORRUPT;
    static constexpr LoadErrorCode MALFORMED = LoadErrorCode::MALFORMED;
    static constexpr LoadErrorCode UNSUPPORTED_VERSION = LoadErrorCode::UNSUPPORTED_VERSION;
    static constexpr LoadErrorCode IO_ERROR = LoadErrorCode::IO_ERROR;

    LoadStatus() = default;
    LoadStatus(LoadErrorCode c) : errorCode(c) {}
    LoadStatus(LoadErrorCode c, std::string msg, int line = 0, std::string orig = "", std::string quar = "")
        : errorCode(c), errorMessage(std::move(msg)), lineNumber(line), originalPath(std::move(orig)), quarantinePath(std::move(quar)) {}

    bool isOk() const { return errorCode == LoadErrorCode::OK; }

    bool operator==(LoadErrorCode c) const {
        if (errorCode == c) return true;
        if (c == LoadErrorCode::CORRUPT &&
            (errorCode == LoadErrorCode::MALFORMED ||
             errorCode == LoadErrorCode::UNSUPPORTED_VERSION ||
             errorCode == LoadErrorCode::IO_ERROR)) {
            return true;
        }
        return false;
    }
    bool operator!=(LoadErrorCode c) const { return !(*this == c); }

    bool operator==(const LoadStatus& other) const { return errorCode == other.errorCode; }
    bool operator!=(const LoadStatus& other) const { return errorCode != other.errorCode; }
    explicit operator bool() const { return isOk(); }
};

inline bool operator==(LoadErrorCode c, const LoadStatus& s) { return s == c; }
inline bool operator!=(LoadErrorCode c, const LoadStatus& s) { return !(s == c); }

} // namespace bms::infra
