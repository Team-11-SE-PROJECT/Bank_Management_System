#pragma once

#include <string>

namespace bms::infra {

// Atomically writes `contents` to `path`:
//   1. write to path + ".tmp"
//   2. flush and close
//   3. on Windows: MoveFileExW with MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH
//      on POSIX: fs::rename
// Returns true on success. If it fails, the original file is untouched and .tmp is preserved.
bool atomicWrite(const std::string& path, const std::string& contents);

// Reads the whole file into a string. Returns false if the file cannot be opened.
bool readAll(const std::string& path, std::string& out);

// Ensures `dir` exists (creates it recursively if needed).
bool ensureDir(const std::string& dir);

// Quarantines a corrupt file by copying it to path + ".corrupt.bak".
bool quarantineFile(const std::string& path, std::string& quarantinePathOut);

} // namespace bms::infra
