#include "infra/FileUtil.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace bms::infra {

bool ensureDir(const std::string& dir) {
    if (dir.empty()) return true;
    std::error_code ec;
    fs::create_directories(dir, ec);
    return !ec;
}

bool readAll(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

bool atomicWrite(const std::string& path, const std::string& contents) {
    fs::path dest(path);
    if (dest.has_parent_path()) {
        ensureDir(dest.parent_path().string());
    }

    const std::string tmp = path + ".tmp";
    fs::path tmpPath(tmp);

    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            return false;
        }
        out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        out.flush();
        if (!out) {
            std::error_code ec;
            fs::remove(tmpPath, ec);
            return false;
        }
    } // File is closed here before rename / replacement

#ifdef _WIN32
    // On Windows, MoveFileExW with MOVEFILE_REPLACE_EXISTING atomically moves/replaces
    // both when destination doesn't exist and when destination exists.
    if (MoveFileExW(tmpPath.c_str(), dest.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return true;
    }
    // If replacement fails: leave .tmp in place so data is not lost, original is preserved, return false
    return false;
#else
    std::error_code ec;
    fs::rename(tmpPath, dest, ec);
    if (ec) {
        return false;
    }
    return true;
#endif
}

bool quarantineFile(const std::string& path, std::string& quarantinePathOut) {
    std::error_code ec;
    if (!fs::exists(path, ec)) {
        return false;
    }
    quarantinePathOut = path + ".corrupt.bak";
    fs::copy_file(path, quarantinePathOut, fs::copy_options::overwrite_existing, ec);
    return !ec;
}

} // namespace bms::infra
