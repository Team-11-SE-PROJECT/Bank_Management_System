#include "infra/TimeUtil.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace bms::infra {

std::string nowTimestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);

    std::tm local{};

#ifdef _WIN32
    localtime_s(&local, &time);
#else
    localtime_r(&time, &local);
#endif

    std::ostringstream out;
    out << std::put_time(&local, "%Y-%m-%d %H:%M:%S");
    return out.str();
}

} // namespace bms::infra