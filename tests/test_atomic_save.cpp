#include "infra/FileUtil.h"
#include "TestHarness.h"
#include "TestUtil.h"

#include <filesystem>
#include <fstream>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using namespace bms::infra;
using namespace bms::test;

static void test_first_write_no_destination() {
    TempDir dir;
    const std::string file = dir.file("first_write.dat");
    const std::string tmp = file + ".tmp";

    CHECK(!exists(file));
    CHECK(!exists(tmp));

    const std::string data = "Hello, first write!";
    CHECK(atomicWrite(file, data));

    CHECK(exists(file));
    CHECK(!exists(tmp)); // tmp must be cleaned up / moved

    std::string readBack;
    CHECK(readAll(file, readBack));
    CHECK_EQ(readBack, data);
}

static void test_replace_existing_destination() {
    TempDir dir;
    const std::string file = dir.file("replace_me.dat");
    const std::string tmp = file + ".tmp";

    // Initial write
    CHECK(atomicWrite(file, "Version 1 content"));
    CHECK_EQ(readFile(file), "Version 1 content");
    CHECK(!exists(tmp));

    // Replacement write
    CHECK(atomicWrite(file, "Version 2 updated content"));
    CHECK_EQ(readFile(file), "Version 2 updated content");
    CHECK(!exists(tmp));
}

static void test_nested_directory_creation() {
    TempDir dir;
    const std::string file = dir.file("nested/sub/dir/data.dat");

    CHECK(atomicWrite(file, "Nested content"));
    CHECK_EQ(readFile(file), "Nested content");
    CHECK(!exists(file + ".tmp"));
}

static void test_write_failure_preserves_original() {
    TempDir dir;
    const std::string file = dir.file("locked_target.dat");
    const std::string tmp = file + ".tmp";

    // Create original file
    const std::string originalData = "Important original data";
    CHECK(atomicWrite(file, originalData));
    CHECK_EQ(readFile(file), originalData);

    // Make target file inaccessible for replacement by locking it or making it a directory
    // If destination is a directory, MoveFileEx cannot replace a directory with a file!
    const std::string dirAsFile = dir.file("dir_conflict");
    std::filesystem::create_directories(dirAsFile);

    // atomicWrite to dir_conflict should fail
    CHECK(!atomicWrite(dirAsFile, "Cannot overwrite directory"));

    // Also test: if destination is opened exclusively without sharing
#ifdef _WIN32
    HANDLE hFile = CreateFileA(
        file.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0, // NO SHARING - completely locked!
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (hFile != INVALID_HANDLE_VALUE) {
        // Attempting to atomicWrite should fail because file is locked exclusively
        bool ok = atomicWrite(file, "Malicious overwrite");
        CHECK(!ok);

        // Close lock
        CloseHandle(hFile);

        // Original content must be preserved!
        CHECK_EQ(readFile(file), originalData);
        // And the .tmp file was left in place on replacement failure
        CHECK(exists(tmp));

        // Clean up tmp manually for cleanliness
        std::error_code ec;
        std::filesystem::remove(tmp, ec);
    }
#endif
}

static void test_repeated_atomic_writes() {
    TempDir dir;
    const std::string file = dir.file("counter.dat");

    for (int i = 0; i < 20; ++i) {
        std::string content = "Iteration " + std::to_string(i);
        CHECK(atomicWrite(file, content));
        CHECK_EQ(readFile(file), content);
        CHECK(!exists(file + ".tmp"));
    }
}

int main() {
    test_first_write_no_destination();
    test_replace_existing_destination();
    test_nested_directory_creation();
    test_write_failure_preserves_original();
    test_repeated_atomic_writes();

    return summary("test_atomic_save");
}
