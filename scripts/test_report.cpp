#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace {

struct TestResult {
    std::string name;
    bool passed = false;
};

struct CoverageResult {
    std::string source;
    std::size_t coveredLines = 0;
    std::size_t totalLines = 0;
    std::size_t coveredBranches = 0;
    std::size_t totalBranches = 0;
    std::size_t coveredNonThrowBranches = 0;
    std::size_t totalNonThrowBranches = 0;
};

std::string readFile(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {};
    }

    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}

void writeFile(const fs::path& path, const std::string& contents) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        throw std::runtime_error("Cannot create file: " + path.string());
    }
    output << contents;
    if (!output) {
        throw std::runtime_error("Cannot write file: " + path.string());
    }
}

std::string shellQuote(const std::string& value) {
#ifdef _WIN32
    std::string quoted = "\"";
    for (char ch : value) {
        if (ch == '"') {
            quoted += "\\\"";
        } else {
            quoted += ch;
        }
    }
    quoted += '"';
    return quoted;
#else
    std::string quoted = "'";
    for (char ch : value) {
        if (ch == '\'') {
            quoted += "'\\''";
        } else {
            quoted += ch;
        }
    }
    quoted += '\'';
    return quoted;
#endif
}

int runCommand(
    const std::string& command,
    const fs::path& logPath,
    bool printOutput = true,
    bool printCommand = false
) {
    if (printCommand) {
        std::cout << "> " << command << '\n';
    }
    const std::string wrapped =
        command + " > " + shellQuote(logPath.string()) + " 2>&1";
    const int status = std::system(wrapped.c_str());
    const std::string output = readFile(logPath);
    if (printOutput) {
        std::cout << output;
        std::cout.flush();
    }
    return status;
}

fs::path findExecutable(const std::string& name) {
    const char* pathValue = std::getenv("PATH");
    if (pathValue == nullptr) {
        throw std::runtime_error("PATH is not set; cannot find " + name);
    }

#ifdef _WIN32
    constexpr char separator = ';';
    const std::vector<std::string> extensions{".exe", ".cmd", ".bat", ""};
#else
    constexpr char separator = ':';
    const std::vector<std::string> extensions{""};
#endif

    std::stringstream paths(pathValue);
    std::string directory;
    while (std::getline(paths, directory, separator)) {
        for (const std::string& extension : extensions) {
            const fs::path candidate =
                fs::path(directory) / (name + extension);
            if (fs::is_regular_file(candidate)) {
                return fs::absolute(candidate);
            }
        }
    }

    throw std::runtime_error(name + " was not found on PATH");
}

fs::path findRepositoryRoot() {
    fs::path current = fs::current_path();
    while (!current.empty()) {
        if (fs::is_regular_file(current / "CMakeLists.txt") &&
            fs::is_directory(current / "tests")) {
            return current;
        }
        const fs::path parent = current.parent_path();
        if (parent == current) {
            break;
        }
        current = parent;
    }
    throw std::runtime_error(
        "Run this program from the repository root or one of its subfolders"
    );
}

bool isDigits(const std::string& value) {
    return !value.empty() &&
        std::all_of(value.begin(), value.end(), [](unsigned char ch) {
            return std::isdigit(ch) != 0;
        });
}

std::vector<TestResult> parseTestResults(const std::string& output) {
    const std::regex testLine(
        R"(^\s*\d+/\d+\s+Test\s+#\d+:\s+(\S+).*?\b(Passed|Failed|Not Run)\b)",
        std::regex::icase
    );
    std::istringstream lines(output);
    std::string line;
    std::vector<TestResult> results;

    while (std::getline(lines, line)) {
        std::smatch match;
        if (std::regex_search(line, match, testLine)) {
            std::string status = match[2].str();
            std::transform(
                status.begin(), status.end(), status.begin(),
                [](unsigned char ch) {
                    return static_cast<char>(std::tolower(ch));
                }
            );
            results.push_back({match[1].str(), status == "passed"});
        }
    }
    return results;
}

bool parseGcovFile(const fs::path& path, CoverageResult& result) {
    std::ifstream input(path);
    if (!input) {
        return false;
    }

    static const std::regex branchLine(
        R"(^\s*branch\s+\d+\s+(taken\s+(\d+)|never executed)\b)"
    );
    std::string line;
    while (std::getline(input, line)) {
        std::smatch branchMatch;
        if (std::regex_search(line, branchMatch, branchLine)) {
            ++result.totalBranches;
            const bool covered = branchMatch[2].matched &&
                std::stoull(branchMatch[2].str()) > 0;
            if (covered) {
                ++result.coveredBranches;
            }
            if (line.find("(throw)") == std::string::npos) {
                ++result.totalNonThrowBranches;
                if (covered) {
                    ++result.coveredNonThrowBranches;
                }
            }
            continue;
        }

        const std::size_t firstColon = line.find(':');
        if (firstColon == std::string::npos) {
            continue;
        }
        const std::size_t secondColon = line.find(':', firstColon + 1);
        if (secondColon == std::string::npos) {
            continue;
        }

        std::string count = line.substr(0, firstColon);
        count.erase(
            std::remove_if(
                count.begin(), count.end(),
                [](unsigned char ch) { return std::isspace(ch) != 0; }
            ),
            count.end()
        );
        std::string lineNumber =
            line.substr(firstColon + 1, secondColon - firstColon - 1);
        lineNumber.erase(
            std::remove_if(
                lineNumber.begin(), lineNumber.end(),
                [](unsigned char ch) { return std::isspace(ch) != 0; }
            ),
            lineNumber.end()
        );
        if (!isDigits(lineNumber) || count.empty() || count == "-") {
            continue;
        }

        ++result.totalLines;
        const std::size_t firstNonDigit =
            count.find_first_not_of("0123456789");
        if (firstNonDigit != 0 &&
            std::stoull(count.substr(0, firstNonDigit)) > 0) {
            ++result.coveredLines;
        }
    }
    return true;
}

std::string htmlEscape(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (char ch : value) {
        switch (ch) {
            case '&': escaped += "&amp;"; break;
            case '<': escaped += "&lt;"; break;
            case '>': escaped += "&gt;"; break;
            case '"': escaped += "&quot;"; break;
            case '\'': escaped += "&#39;"; break;
            default: escaped += ch; break;
        }
    }
    return escaped;
}

double percent(std::size_t covered, std::size_t total) {
    return total == 0 ? 0.0 :
        100.0 * static_cast<double>(covered) /
        static_cast<double>(total);
}

std::string formatPercent(std::size_t covered, std::size_t total) {
    std::ostringstream value;
    value.precision(2);
    value << std::fixed << percent(covered, total) << '%';
    return value.str();
}

std::string testTable(const std::vector<TestResult>& results) {
    std::ostringstream html;
    html << "<table><thead><tr><th>Test</th><th>Result</th></tr></thead><tbody>";
    for (const TestResult& result : results) {
        html << "<tr><td>" << htmlEscape(result.name) << "</td><td class=\""
             << (result.passed ? "pass" : "fail") << "\">"
             << (result.passed ? "PASS" : "FAIL")
             << "</td></tr>";
    }
    html << "</tbody></table>";
    return html.str();
}

std::string coverageTable(
    const std::vector<CoverageResult>& results,
    int metric
) {
    std::ostringstream html;
    html << "<table><thead><tr><th>Source file</th><th>Covered</th>"
         << "<th>Total</th><th>Coverage</th></tr></thead><tbody>";

    std::size_t coveredTotal = 0;
    std::size_t total = 0;
    for (const CoverageResult& result : results) {
        const std::size_t covered = metric == 0
            ? result.coveredLines
            : metric == 1
                ? result.coveredBranches
                : result.coveredNonThrowBranches;
        const std::size_t count = metric == 0
            ? result.totalLines
            : metric == 1
                ? result.totalBranches
                : result.totalNonThrowBranches;
        coveredTotal += covered;
        total += count;
        html << "<tr><td>" << htmlEscape(result.source) << "</td><td>"
             << covered << "</td><td>" << count << "</td><td>"
             << formatPercent(covered, count) << "</td></tr>";
    }

    html << "<tr class=\"total\"><td>TOTAL</td><td>" << coveredTotal
         << "</td><td>" << total << "</td><td>"
         << formatPercent(coveredTotal, total) << "</td></tr>"
         << "</tbody></table>";
    return html.str();
}

std::string fileUri(const fs::path& path) {
    const std::string generic = fs::absolute(path).generic_string();
    std::string uri = "file:///";
    for (unsigned char ch : generic) {
        if (ch == ' ') {
            uri += "%20";
        } else if (ch == '#') {
            uri += "%23";
        } else if (ch == '%') {
            uri += "%25";
        } else {
            uri += static_cast<char>(ch);
        }
    }
    return uri;
}

fs::path findEdge() {
#ifdef _WIN32
    const char* variables[] = {
        "PROGRAMFILES(X86)", "PROGRAMFILES", "LOCALAPPDATA"
    };
    const std::vector<fs::path> suffixes = {
        fs::path("Microsoft/Edge/Application/msedge.exe"),
        fs::path("Microsoft/Edge/Application/msedge.exe"),
        fs::path("Microsoft/Edge/Application/msedge.exe")
    };
    for (std::size_t index = 0; index < 3; ++index) {
        const char* value = std::getenv(variables[index]);
        if (value != nullptr) {
            const fs::path candidate = fs::path(value) / suffixes[index];
            if (fs::is_regular_file(candidate)) {
                return fs::absolute(candidate).make_preferred();
            }
        }
    }
#endif
    return findExecutable("msedge");
}

#ifdef _WIN32
std::wstring quoteWindowsArgument(const std::wstring& argument) {
    std::wstring quoted = L"\"";
    std::size_t backslashes = 0;
    for (wchar_t ch : argument) {
        if (ch == L'\\') {
            ++backslashes;
        } else if (ch == L'"') {
            quoted.append(backslashes * 2 + 1, L'\\');
            quoted += ch;
            backslashes = 0;
        } else {
            quoted.append(backslashes, L'\\');
            quoted += ch;
            backslashes = 0;
        }
    }
    quoted.append(backslashes * 2, L'\\');
    quoted += L'"';
    return quoted;
}

int createPdf(
    const fs::path& edge,
    const fs::path& profile,
    const fs::path& pdfPath,
    const fs::path& htmlPath,
    const fs::path& workingDirectory
) {
    const std::string htmlUri = fileUri(htmlPath);
    const std::wstring htmlUriWide(htmlUri.begin(), htmlUri.end());
    const std::vector<std::wstring> arguments = {
        edge.wstring(),
        L"--headless=new",
        L"--disable-gpu",
        L"--no-first-run",
        L"--no-pdf-header-footer",
        L"--user-data-dir=" + fs::absolute(profile).wstring(),
        L"--print-to-pdf=" + fs::absolute(pdfPath).wstring(),
        htmlUriWide
    };

    std::wstring commandLine;
    for (const std::wstring& argument : arguments) {
        if (!commandLine.empty()) {
            commandLine += L' ';
        }
        commandLine += quoteWindowsArgument(argument);
    }

    std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const std::wstring workingDirectoryWide = workingDirectory.wstring();
    if (!CreateProcessW(
            edge.c_str(),
            mutableCommand.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW,
            nullptr,
            workingDirectoryWide.c_str(),
            &startup,
            &process
        )) {
        return -static_cast<int>(GetLastError());
    }

    const DWORD waitResult = WaitForSingleObject(process.hProcess, 90000);
    DWORD exitCode = 1;
    if (waitResult == WAIT_OBJECT_0) {
        if (!GetExitCodeProcess(process.hProcess, &exitCode)) {
            exitCode = GetLastError();
        }
    } else {
        exitCode = waitResult == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError();
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return static_cast<int>(exitCode);
}
#endif

std::string currentDateTime() {
    const std::time_t now = std::time(nullptr);
    std::tm localTime{};
#ifdef _WIN32
    localtime_s(&localTime, &now);
#else
    localtime_r(&now, &localTime);
#endif
    std::ostringstream date;
    date << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S");
    return date.str();
}

std::string htmlStart(const std::string& title) {
    std::ostringstream html;
    html << "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">"
         << "<title>" << htmlEscape(title) << "</title>"
         << "<style>"
         << "@page{size:A4;margin:16mm}"
         << "body{font:10pt/1.45 Arial,sans-serif;color:#202833}"
         << "h1{color:#153a63;font-size:23pt}"
         << "h2{color:#194f80;border-bottom:1px solid #b8c5d1;"
            "padding-bottom:4pt;margin-top:22pt}"
         << "table{width:100%;border-collapse:collapse;font-size:8.5pt;"
            "margin:10pt 0 16pt}"
         << "th{background:#e8f0f7;color:#153a63;text-align:left}"
         << "td,th{border:1px solid #aebdca;padding:4pt 5pt}"
         << "tr{page-break-inside:avoid}"
         << ".total{font-weight:bold;background:#e8f0f7}"
         << ".pass{color:#16703b;font-weight:bold}"
         << ".fail{color:#a11616;font-weight:bold}"
         << ".note{color:#536273;font-size:9pt}"
         << "</style></head><body>"
         << "<h1>Bank Management System</h1>"
         << "<p><strong>" << htmlEscape(title)
         << "</strong><br>Generated: " << htmlEscape(currentDateTime())
         << "</p>";
    return html.str();
}

std::string createUnitTestHtml(
    const std::vector<TestResult>& tests,
    int testRunStatus
) {
    const std::size_t passed = static_cast<std::size_t>(
        std::count_if(tests.begin(), tests.end(), [](const TestResult& test) {
            return test.passed;
        })
    );

    std::ostringstream html;
    html << htmlStart("Unit Test Report")
         << "<p>" << passed << " of " << tests.size()
         << " CTest targets passed. Build/CTest exit code: "
         << testRunStatus << ".</p>"
         << testTable(tests)
         << "<p class=\"note\">System validation is reported separately in "
            "SYSTEM_VALIDATION_REPORT.pdf.</p>"
         << "</body></html>";
    return html.str();
}

std::string createSystemValidationHtml(
    const std::vector<TestResult>& systemTests,
    int systemRunStatus
) {
    const std::size_t passed = static_cast<std::size_t>(
        std::count_if(
            systemTests.begin(), systemTests.end(),
            [](const TestResult& test) { return test.passed; }
        )
    );

    std::ostringstream html;
    html << htmlStart("System Validation Report")
         << "<p>" << passed << " of " << systemTests.size()
         << " dedicated system-validation test passed. CTest exit code: "
         << systemRunStatus << ".</p>"
         << testTable(systemTests)
         << "<p>The end-to-end system test exercises account creation, customer "
            "authentication and role access, deposit, transfer, withdrawal, "
            "invalid transaction rejection, persisted balances, transaction "
            "state, audit events, and logout.</p>"
         << "</body></html>";
    return html.str();
}

std::string createCoverageHtml(
    const std::vector<CoverageResult>& coverage
) {
    std::ostringstream html;
    html << htmlStart("Code and Branch Coverage Report")
         << "<h2>Code Coverage</h2>"
         << "<p>Line coverage is measured in C++ implementation sources; "
            "the interactive src/main.cpp entry point is excluded.</p>"
         << coverageTable(coverage, 0)
         << "<h2>Raw GCC Branch Outcomes</h2>"
         << "<p>Raw gcov counts every compiler-reported branch outcome, "
            "including exception edges. Headers, tests, and src/main.cpp "
            "are excluded.</p>"
         << coverageTable(coverage, 1)
         << "<h2>Non-Throw Branch Outcomes</h2>"
         << "<p>This supplemental metric excludes only outcomes explicitly "
            "marked as throw edges by gcov; it is not a replacement for raw "
            "branch coverage. Outcomes are counted as covered when taken at "
            "least once.</p>"
         << coverageTable(coverage, 2)
         << "<p class=\"note\">Coverage was collected with GCC gcov after the "
            "instrumented CTest run. A missing or unmeasured source file is "
            "reported as an error instead of being silently omitted.</p>"
         << "</body></html>";
    return html.str();
}

} // namespace

int main() {
    try {
#ifndef _WIN32
        throw std::runtime_error(
            "This PDF runner currently targets Windows with MinGW GCC and Microsoft Edge."
        );
#endif
        const fs::path root = findRepositoryRoot();
        fs::current_path(root);

        const fs::path buildDirectory = root / "build-test-report";
        const fs::path outputDirectory = buildDirectory / "gcov";
        const fs::path logPath = buildDirectory / "command-output.txt";
        const fs::path docsDirectory = root / "Docs";
        fs::create_directories(buildDirectory);
        fs::create_directories(docsDirectory);

        const fs::path compiler = findExecutable("g++");
        const fs::path gcov = compiler.parent_path() / "gcov.exe";
        if (!fs::is_regular_file(gcov)) {
            throw std::runtime_error(
                "Could not find matching gcov.exe beside " + compiler.string()
            );
        }

        std::cout << "Configuring coverage-enabled C++ tests...\n";
        const std::string configure =
            "cmake -S . -B build-test-report -G \"MinGW Makefiles\""
            " -DCMAKE_BUILD_TYPE=Debug"
            " -DCMAKE_CXX_COMPILER=" + shellQuote(compiler.string()) +
            " \"-DCMAKE_CXX_FLAGS=--coverage -O0 -g\""
            " -DCMAKE_EXE_LINKER_FLAGS=--coverage";
        if (runCommand(configure, logPath) != 0) {
            throw std::runtime_error(
                "CMake configuration failed; see build-test-report/command-output.txt"
            );
        }

        std::cout << "\nBuilding test targets...\n";
        if (runCommand(
                "cmake --build build-test-report --config Debug",
                logPath
            ) != 0) {
            throw std::runtime_error(
                "C++ test build failed; see build-test-report/command-output.txt"
            );
        }

        for (const fs::directory_entry& entry :
             fs::recursive_directory_iterator(buildDirectory)) {
            if (entry.is_regular_file() && entry.path().extension() == ".gcda") {
                fs::remove(entry.path());
            }
        }

        std::cout << "\nRunning the complete unit and integration test suite...\n";
        const int testRunStatus = runCommand(
            "ctest --test-dir build-test-report -C Debug --output-on-failure",
            logPath
        );
        const std::string testOutput = readFile(logPath);
        std::vector<TestResult> tests = parseTestResults(testOutput);
        tests.erase(
            std::remove_if(
                tests.begin(), tests.end(),
                [](const TestResult& test) {
                    return test.name == "test_system_validation";
                }
            ),
            tests.end()
        );

        std::cout << "\nRunning the system-validation test independently...\n";
        const int systemRunStatus = runCommand(
            "ctest --test-dir build-test-report -C Debug "
            "-R \"^test_system_validation$\" --output-on-failure",
            logPath
        );
        const std::vector<TestResult> systemTests =
            parseTestResults(readFile(logPath));

        if (tests.empty()) {
            throw std::runtime_error(
                "CTest output did not contain any test results; "
                "see build-test-report/command-output.txt"
            );
        }
        if (systemTests.empty()) {
            throw std::runtime_error(
                "The dedicated system-validation test did not run"
            );
        }

        fs::create_directories(outputDirectory);
        std::vector<CoverageResult> coverage;
        for (const fs::directory_entry& entry :
             fs::recursive_directory_iterator(root / "src")) {
            if (!entry.is_regular_file() ||
                entry.path().extension() != ".cpp" ||
                entry.path().filename() == "main.cpp") {
                continue;
            }

            const fs::path sourcePath = entry.path();
            const fs::path relative = fs::relative(sourcePath, root);
            const fs::path objectPath =
                buildDirectory / "CMakeFiles" / "bms_core.dir" /
                (relative.string() + ".obj");
            if (!fs::is_regular_file(objectPath)) {
                throw std::runtime_error(
                    "Coverage object not found for " + relative.string()
                );
            }

            const std::string gcovCommand =
                "cd /d " + shellQuote(outputDirectory.string()) +
                " && " + shellQuote(gcov.string()) + " -b -c -o " +
                shellQuote(fs::absolute(objectPath).string()) + " " +
                shellQuote(fs::absolute(sourcePath).string());
            if (runCommand(gcovCommand, logPath, false) != 0) {
                throw std::runtime_error(
                    "gcov failed for " + relative.string()
                );
            }

            const fs::path reportPath =
                outputDirectory / (sourcePath.filename().string() + ".gcov");
            CoverageResult result;
            result.source = relative.generic_string();
            if (!parseGcovFile(reportPath, result)) {
                throw std::runtime_error(
                    "Could not read gcov report for " + relative.string()
                );
            }
            coverage.push_back(result);
        }
        if (coverage.empty()) {
            throw std::runtime_error(
                "No C++ source coverage data was collected"
            );
        }

        const fs::path edge = findEdge();
        const std::vector<std::pair<std::string, std::string>> reports = {
            {
                "UNIT_TEST_REPORT",
                createUnitTestHtml(tests, testRunStatus)
            },
            {
                "SYSTEM_VALIDATION_REPORT",
                createSystemValidationHtml(systemTests, systemRunStatus)
            },
            {
                "CODE_AND_BRANCH_COVERAGE_REPORT",
                createCoverageHtml(coverage)
            }
        };

        std::cout << "\nGenerating three separate PDFs with Microsoft Edge...\n";
        for (const auto& report : reports) {
            const fs::path htmlPath =
                buildDirectory / (report.first + ".html");
            const fs::path pdfPath =
                docsDirectory / (report.first + ".pdf");
            const fs::path edgeProfile =
                buildDirectory / (report.first + "-edge-profile");
            writeFile(htmlPath, report.second);
#ifdef _WIN32
            const int pdfStatus = createPdf(
                edge, edgeProfile, pdfPath, htmlPath, root
            );
#else
            const int pdfStatus = -1;
#endif
            const bool pdfExists = fs::is_regular_file(pdfPath);
            if (pdfStatus != 0 || !pdfExists || fs::file_size(pdfPath) == 0) {
                throw std::runtime_error(
                    "PDF generation failed for " + pdfPath.filename().string() +
                    " (Edge exit code " + std::to_string(pdfStatus) +
                    ", output exists " + (pdfExists ? "yes" : "no") +
                    "); see build-test-report/command-output.txt"
                );
            }
            std::cout << "Created: " << pdfPath.string() << '\n';
        }

        std::error_code removeError;
        fs::remove(docsDirectory / "TEST_REPORT.pdf", removeError);
        if (removeError) {
            throw std::runtime_error(
                "Could not remove obsolete Docs/TEST_REPORT.pdf: " +
                removeError.message()
            );
        }

        std::cout << "\nUnit tests: " << tests.size()
                  << ", passed: "
                  << std::count_if(
                         tests.begin(), tests.end(),
                         [](const TestResult& test) { return test.passed; }
                     )
                  << '\n'
                  << "System validation tests: " << systemTests.size()
                  << ", passed: "
                  << std::count_if(
                         systemTests.begin(), systemTests.end(),
                         [](const TestResult& test) { return test.passed; }
                     )
                  << '\n'
                  << "Measured C++ source files: " << coverage.size() << '\n';

        return testRunStatus == 0 && systemRunStatus == 0 ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "Test report failed: " << error.what() << '\n';
        return 1;
    }
}
