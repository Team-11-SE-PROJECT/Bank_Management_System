#pragma once
#include <string>
namespace bms::infra {
// Atomically writes `contents` to `path`:
//   1. write to path + ".tmp"
//   2. flush + fsync (best-effort on Windows)
//   3. rename over the original
// Returns true on success. If it fails, the original file is untouched.
bool atomicWrite(const std::string& path, const std::string& contents);
// Reads the whole file into a string. Returns false if the file cannot be opened.
bool readAll(const std::string& path, std::string& out);
// Ensures `dir` exists (creates it recursively if needed).
bool ensureDir(const std::string& dir);
} // namespace bms::infra