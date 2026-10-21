#include "infra/FileUtil.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <iterator>
namespace fs = std::filesystem;
namespace bms::infra {
bool ensureDir(const std::string& dir) {
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
    const std::string tmp = path + ".tmp";

    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        out.flush();
        if (!out) return false;
    } // file closed here

    std::error_code ec;
    fs::rename(tmp, path, ec);
    if (ec) {
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

} // namespace bms::infra