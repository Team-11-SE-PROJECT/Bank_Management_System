#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>

namespace bms::test {

class TempDir {
public:
    TempDir() {
        static std::atomic<unsigned long long> counter{0};

        const auto timestamp =
            std::chrono::steady_clock::now()
                .time_since_epoch()
                .count();

        const auto id = counter.fetch_add(1);

        path_ = std::filesystem::temp_directory_path() /
                ("bms_test_" + std::to_string(timestamp) +
                 "_" + std::to_string(id));

        std::error_code ec;
        std::filesystem::create_directories(path_, ec);

        if (ec) {
            throw std::runtime_error(
                "Failed to create temporary test directory: " +
                ec.message()
            );
        }
    }

    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    std::string file(const std::string& name) const {
        return (path_ / name).string();
    }

private:
    std::filesystem::path path_;
};

inline void writeFile(
    const std::string& path,
    const std::string& contents
) {
    const std::filesystem::path p(path);

    if (!p.parent_path().empty()) {
        std::filesystem::create_directories(p.parent_path());
    }

    std::ofstream out(path, std::ios::binary);

    if (!out) {
        throw std::runtime_error("Cannot write test file: " + path);
    }

    out.write(
        contents.data(),
        static_cast<std::streamsize>(contents.size())
    );

    out.close();

    if (!out) {
        throw std::runtime_error("Failed writing test file: " + path);
    }
}

inline std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);

    if (!in) {
        throw std::runtime_error("Cannot read test file: " + path);
    }

    return std::string(
        std::istreambuf_iterator<char>(in),
        std::istreambuf_iterator<char>()
    );
}

inline bool exists(const std::string& path) {
    return std::filesystem::exists(path);
}

} // namespace bms::test