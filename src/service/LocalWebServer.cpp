#include "service/LocalWebServer.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "domain/Status.h"
#include "infra/InputValidator.h"
#include "service/AccountDetailsService.h"
#include "service/SessionManager.h"

#ifndef BMS_WEB_ROOT
#define BMS_WEB_ROOT "web"
#endif

namespace bms::service {

namespace {

#ifdef _WIN32
using Socket = SOCKET;
constexpr Socket kInvalidSocket = INVALID_SOCKET;
void closeSocket(Socket socket) { closesocket(socket); }
#else
using Socket = int;
constexpr Socket kInvalidSocket = -1;
void closeSocket(Socket socket) { close(socket); }
#endif

constexpr std::size_t kMaxHeaderBytes = 16 * 1024;
constexpr std::size_t kMaxBodyBytes = 16 * 1024;

struct HttpRequest {
    std::string method;
    std::string path;
    std::map<std::string, std::string> headers;
    std::map<std::string, std::string> form;
};

struct HttpResponse {
    int status = 200;
    std::string contentType = "application/json; charset=utf-8";
    std::string body;
    std::string cookie;
};

std::string trim(const std::string& value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

std::string jsonString(const std::string& value) {
    std::string result = "\"";
    for (unsigned char ch : value) {
        switch (ch) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\b': result += "\\b"; break;
            case '\f': result += "\\f"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:
                if (ch < 0x20) {
                    static const char digits[] = "0123456789abcdef";
                    result += "\\u00";
                    result += digits[(ch >> 4) & 0x0f];
                    result += digits[ch & 0x0f];
                } else {
                    result += static_cast<char>(ch);
                }
        }
    }
    result += '"';
    return result;
}

std::string jsonError(const std::string& message) {
    return "{\"ok\":false,\"message\":" + jsonString(message) + "}";
}

std::string transactionError(TxnStatus status) {
    switch (status) {
        case TxnStatus::OK: return "Success.";
        case TxnStatus::INVALID_INPUT:
            return "Enter a positive amount with at most two decimal places.";
        case TxnStatus::NOT_FOUND: return "Account not found.";
        case TxnStatus::ACCOUNT_CLOSED: return "This account is closed.";
        case TxnStatus::INSUFFICIENT_FUNDS: return "Insufficient balance.";
        case TxnStatus::LIMIT_EXCEEDED:
            return "Daily withdrawal limit exceeded.";
        case TxnStatus::STORAGE_FAILURE:
            return "Storage error. Please check the data files.";
    }
    return "Transaction failed.";
}

bool decodeFormComponent(const std::string& input, std::string& output) {
    output.clear();
    for (std::size_t i = 0; i < input.size(); ++i) {
        const char ch = input[i];
        if (ch == '+') {
            output += ' ';
        } else if (ch == '%') {
            if (i + 2 >= input.size()) {
                return false;
            }
            auto hex = [](char value) -> int {
                if (value >= '0' && value <= '9') return value - '0';
                if (value >= 'a' && value <= 'f') return value - 'a' + 10;
                if (value >= 'A' && value <= 'F') return value - 'A' + 10;
                return -1;
            };
            const int high = hex(input[i + 1]);
            const int low = hex(input[i + 2]);
            if (high < 0 || low < 0) {
                return false;
            }
            const char decoded = static_cast<char>((high << 4) | low);
            if (decoded == '\0') {
                return false;
            }
            output += decoded;
            i += 2;
        } else {
            output += ch;
        }
    }
    return true;
}

bool parseForm(const std::string& body,
               std::map<std::string, std::string>& values) {
    std::size_t start = 0;
    while (start < body.size()) {
        const auto amp = body.find('&', start);
        const auto end = amp == std::string::npos ? body.size() : amp;
        const auto equals = body.find('=', start);
        if (equals == std::string::npos || equals >= end) {
            return false;
        }
        std::string key;
        std::string value;
        if (!decodeFormComponent(body.substr(start, equals - start), key) ||
            !decodeFormComponent(body.substr(equals + 1, end - equals - 1), value) ||
            key.empty() || !values.emplace(key, value).second) {
            return false;
        }
        if (amp == std::string::npos) {
            break;
        }
        start = amp + 1;
    }
    return true;
}

bool sendAll(Socket socket, const std::string& message) {
    std::size_t sent = 0;
    while (sent < message.size()) {
#ifdef _WIN32
        const int count = ::send(
            socket, message.data() + sent,
            static_cast<int>(message.size() - sent), 0
        );
#else
        const auto count = ::send(
            socket, message.data() + sent, message.size() - sent, 0
        );
#endif
        if (count <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(count);
    }
    return true;
}

bool readRequest(Socket socket, HttpRequest& request, int& errorStatus) {
    std::string data;
    std::array<char, 4096> buffer{};
    std::size_t headerEnd = std::string::npos;
    std::size_t contentLength = 0;

    while (headerEnd == std::string::npos) {
#ifdef _WIN32
        const int count = ::recv(socket, buffer.data(),
                                 static_cast<int>(buffer.size()), 0);
#else
        const auto count = ::recv(socket, buffer.data(), buffer.size(), 0);
#endif
        if (count <= 0) {
            return false;
        }
        data.append(buffer.data(), static_cast<std::size_t>(count));
        if (data.size() > kMaxHeaderBytes + kMaxBodyBytes) {
            errorStatus = 413;
            return false;
        }
        headerEnd = data.find("\r\n\r\n");
        if (headerEnd == std::string::npos &&
            data.size() > kMaxHeaderBytes) {
            errorStatus = 431;
            return false;
        }
    }
    if (headerEnd > kMaxHeaderBytes) {
        errorStatus = 431;
        return false;
    }

    const std::string headers = data.substr(0, headerEnd);
    std::istringstream input(headers);
    std::string line;
    if (!std::getline(input, line)) {
        errorStatus = 400;
        return false;
    }
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    std::istringstream requestLine(line);
    std::string target;
    std::string version;
    std::string extra;
    if (!(requestLine >> request.method >> target >> version) ||
        (requestLine >> extra) ||
        (version != "HTTP/1.1" && version != "HTTP/1.0") ||
        target.empty() || target[0] != '/') {
        errorStatus = 400;
        return false;
    }
    const auto query = target.find('?');
    request.path = target.substr(0, query);

    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const auto colon = line.find(':');
        if (colon == std::string::npos || colon == 0) {
            errorStatus = 400;
            return false;
        }
        const std::string name = lower(trim(line.substr(0, colon)));
        const std::string value = trim(line.substr(colon + 1));
        if (!request.headers.emplace(name, value).second) {
            errorStatus = 400;
            return false;
        }
        if (name == "content-length") {
            if (value.empty()) {
                errorStatus = 400;
                return false;
            }
            contentLength = 0;
            for (char digit : value) {
                if (digit < '0' || digit > '9' ||
                    contentLength > kMaxBodyBytes / 10) {
                    errorStatus = 413;
                    return false;
                }
                contentLength = contentLength * 10 +
                    static_cast<std::size_t>(digit - '0');
                if (contentLength > kMaxBodyBytes) {
                    errorStatus = 413;
                    return false;
                }
            }
        }
    }

    if (request.headers.count("transfer-encoding") != 0) {
        errorStatus = 400;
        return false;
    }
    const auto bodyStart = headerEnd + 4;
    while (data.size() - bodyStart < contentLength) {
#ifdef _WIN32
        const int count = ::recv(socket, buffer.data(),
                                 static_cast<int>(buffer.size()), 0);
#else
        const auto count = ::recv(socket, buffer.data(), buffer.size(), 0);
#endif
        if (count <= 0) {
            errorStatus = 400;
            return false;
        }
        data.append(buffer.data(), static_cast<std::size_t>(count));
        if (data.size() - bodyStart > kMaxBodyBytes) {
            errorStatus = 413;
            return false;
        }
    }

    if (contentLength > 0) {
        const auto type = request.headers.find("content-type");
        if (type == request.headers.end() ||
            (type->second != "application/x-www-form-urlencoded" &&
             type->second.find("application/x-www-form-urlencoded;") != 0) ||
            !parseForm(data.substr(bodyStart, contentLength), request.form)) {
            errorStatus = 400;
            return false;
        }
    }
    return true;
}

std::string statusText(int status) {
    switch (status) {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 409: return "Conflict";
        case 423: return "Locked";
        case 413: return "Payload Too Large";
        case 431: return "Request Header Fields Too Large";
        case 500: return "Internal Server Error";
        default: return "Error";
    }
}

std::string get(const std::map<std::string, std::string>& values,
                const std::string& key) {
    const auto found = values.find(key);
    return found == values.end() ? std::string() : found->second;
}

} // namespace

struct LocalWebServer::Impl {
    infra::LoginService& login;
    infra::CredentialStore& credentials;
    infra::AccountRepository& accounts;
    infra::TransactionRepository& transactions;
    infra::CustomerAccountStore& customerData;
    TransactionService& transactionService;
    AccountService& accountService;
    AccountUnlockService& unlockService;
    SystemStatusService& systemStatusService;
    AuthService& authService;
    std::map<std::string, SessionManager> sessions;

    Impl(
        infra::LoginService& loginService,
        infra::CredentialStore& credentialStore,
        infra::AccountRepository& accountRepository,
        infra::TransactionRepository& transactionRepository,
        infra::CustomerAccountStore& customerAccountStore,
        TransactionService& txnService,
        AccountService& acctService,
        AccountUnlockService& acctUnlockService,
        SystemStatusService& statusService,
        AuthService& authenticationService
    )
        : login(loginService),
          credentials(credentialStore),
          accounts(accountRepository),
          transactions(transactionRepository),
          customerData(customerAccountStore),
          transactionService(txnService),
          accountService(acctService),
          unlockService(acctUnlockService),
          systemStatusService(statusService),
          authService(authenticationService) {}

    HttpResponse response(int status, const std::string& body) const {
        return {status, "application/json; charset=utf-8", body, {}};
    }

    HttpResponse error(int status, const std::string& message) const {
        return response(status, jsonError(message));
    }

    std::string token() const {
        static constexpr char hex[] = "0123456789abcdef";
        std::random_device random;
        std::string value;
        value.reserve(64);
        for (int i = 0; i < 32; ++i) {
            const auto byte = static_cast<unsigned int>(random()) & 0xffu;
            value += hex[(byte >> 4) & 0x0fu];
            value += hex[byte & 0x0fu];
        }
        return value;
    }

    std::string sessionToken(const HttpRequest& request) const {
        const auto found = request.headers.find("cookie");
        if (found == request.headers.end()) {
            return {};
        }
        std::istringstream cookies(found->second);
        std::string cookie;
        while (std::getline(cookies, cookie, ';')) {
            const auto equal = cookie.find('=');
            if (equal != std::string::npos &&
                trim(cookie.substr(0, equal)) == "bms_session") {
                return trim(cookie.substr(equal + 1));
            }
        }
        return {};
    }

    SessionManager* session(const HttpRequest& request, int& status) {
        const std::string key = sessionToken(request);
        const auto found = sessions.find(key);
        if (key.empty() || found == sessions.end()) {
            status = 401;
            return nullptr;
        }
        if (!found->second.isActive()) {
            sessions.erase(found);
            status = 401;
            return nullptr;
        }
        found->second.refreshActivity();
        return &found->second;
    }

    bool roleMatches(const SessionManager& session,
                     infra::Role role) const {
        return session.getRole() == role;
    }

    HttpResponse me(SessionManager& session) {
        const std::string userId = session.getUserId();
        const bool admin = session.getRole() == infra::Role::ADMIN;
        std::string body =
            "{\"ok\":true,\"userId\":" + jsonString(userId) +
            ",\"role\":" + jsonString(admin ? "ADMIN" : "CUSTOMER");

        if (!admin) {
            if (!customerData.load()) {
                return error(500, "Could not load customer account data.");
            }
            const auto* account = accounts.findByCustomer(userId);
            if (account == nullptr) {
                body += ",\"account\":null,\"transactions\":[]}";
                return response(200, body);
            }

            const AccountDetailsService details(customerData);
            const auto result =
                details.getByAccountNumber(account->accountNo, userId);
            if (result.status != AccountDetailsStatus::OK) {
                return error(500, "Could not load account details.");
            }
            body += ",\"account\":{\"accountNo\":" +
                jsonString(result.account.accountNo) +
                ",\"status\":" + jsonString(result.account.status) +
                ",\"balancePaise\":" +
                jsonString(std::to_string(result.account.balancePaise)) +
                ",\"customerId\":" + jsonString(result.customer.customerId) +
                ",\"name\":" + jsonString(result.customer.name) +
                ",\"phone\":" + jsonString(result.customer.phone) +
                ",\"address\":" + jsonString(result.customer.address) +
                "},\"transactions\":[";

            bool first = true;
            const auto& all = transactions.all();
            for (auto item = all.rbegin(); item != all.rend(); ++item) {
                if (item->accountNo != account->accountNo ||
                    item->state != domain::TxnState::COMMITTED) {
                    continue;
                }
                if (!first) body += ',';
                first = false;
                body += "{\"txnId\":" + jsonString(item->txnId) +
                    ",\"type\":" + jsonString(domain::toString(item->type)) +
                    ",\"amountPaise\":" +
                    jsonString(std::to_string(item->amountPaise)) +
                    ",\"balanceAfterPaise\":" + jsonString(
                        std::to_string(item->balanceAfterPaise)
                    ) +
                    ",\"counterparty\":" + jsonString(item->counterparty) +
                    ",\"timestamp\":" + jsonString(item->timestamp) + "}";
            }
            body += "]}";
            return response(200, body);
        }

        body += "}";
        return response(200, body);
    }

    HttpResponse adminStatus(SessionManager& session) {
        if (!roleMatches(session, infra::Role::ADMIN)) {
            return error(403, "Administrator access is required.");
        }
        if (!credentials.load() || !customerData.load()) {
            return error(500, "Could not load system status data.");
        }
        SystemStatus status;
        if (systemStatusService.getStatus(session, status) !=
            domain::Status::OK) {
            return error(403, "Administrator access is required.");
        }
        return response(200,
            "{\"ok\":true,\"applicationRunning\":true,"
            "\"credentialsFileAccessible\":" +
            std::string(status.credentialsFileAccessible ? "true" : "false") +
            ",\"customersFileAccessible\":" +
            (status.customersFileAccessible ? "true" : "false") +
            ",\"accountsFileAccessible\":" +
            (status.accountsFileAccessible ? "true" : "false") +
            ",\"credentialCount\":" +
            std::to_string(status.credentialCount) +
            ",\"customerCount\":" + std::to_string(status.customerCount) +
            ",\"accountCount\":" + std::to_string(status.accountCount) + "}"
        );
    }

    HttpResponse handle(const HttpRequest& request, unsigned short port) {
        if (request.path == "/" || request.path == "/index.html" ||
            request.path == "/app.js" || request.path == "/styles.css") {
            if (request.method != "GET") {
                return error(405, "Method not allowed.");
            }
            std::string filename = "index.html";
            std::string type = "text/html; charset=utf-8";
            if (request.path == "/app.js") {
                filename = "app.js";
                type = "text/javascript; charset=utf-8";
            } else if (request.path == "/styles.css") {
                filename = "styles.css";
                type = "text/css; charset=utf-8";
            }
            std::ifstream file(std::string(BMS_WEB_ROOT) + "/" + filename,
                               std::ios::binary);
            if (!file) {
                return error(500, "Web interface assets could not be opened.");
            }
            std::ostringstream content;
            content << file.rdbuf();
            return {200, type, content.str(), {}};
        }

        const auto host = request.headers.find("host");
        const std::string expectedPort = ":" + std::to_string(port);
        if (host == request.headers.end() ||
            (host->second != "127.0.0.1" + expectedPort &&
             host->second != "localhost" + expectedPort)) {
            return error(403, "Requests must use the local web address.");
        }
        const auto origin = request.headers.find("origin");
        if (origin != request.headers.end() &&
            origin->second != "http://127.0.0.1" + expectedPort &&
            origin->second != "http://localhost" + expectedPort) {
            return error(403, "Cross-origin requests are not allowed.");
        }

        if (request.path == "/api/login") {
            if (request.method != "POST") {
                return error(405, "Method not allowed.");
            }
            const std::string roleText = get(request.form, "role");
            infra::Role role;
            if (roleText == "ADMIN") {
                role = infra::Role::ADMIN;
            } else if (roleText == "CUSTOMER") {
                role = infra::Role::CUSTOMER;
            } else {
                return error(400, "Choose a valid user role.");
            }
            const auto result = login.authenticate(
                get(request.form, "userId"),
                get(request.form, "password"),
                role
            );
            switch (result.status) {
                case infra::LoginStatus::INVALID_CREDENTIALS:
                    return error(401, "Invalid user ID or password. " +
                        std::to_string(result.attemptsRemaining) +
                        " attempt(s) remaining.");
                case infra::LoginStatus::ACCOUNT_LOCKED:
                    return error(423, "This account is locked.");
                case infra::LoginStatus::STORAGE_ERROR:
                    return error(500, "Could not save login state.");
                case infra::LoginStatus::SUCCESS:
                    break;
            }
            std::string key;
            try {
                do {
                    key = token();
                } while (sessions.find(key) != sessions.end());
            } catch (const std::exception&) {
                return error(500, "Could not create a secure browser session.");
            }
            SessionManager session;
            session.login(get(request.form, "userId"), role, 300);
            sessions.emplace(key, std::move(session));
            HttpResponse resultResponse = response(
                200,
                "{\"ok\":true,\"userId\":" +
                    jsonString(get(request.form, "userId")) +
                    ",\"role\":" + jsonString(roleText) + "}"
            );
            resultResponse.cookie =
                "bms_session=" + key +
                "; Path=/; HttpOnly; SameSite=Strict; Max-Age=1800";
            return resultResponse;
        }

        if (request.path == "/api/logout") {
            if (request.method != "POST") {
                return error(405, "Method not allowed.");
            }
            const auto key = sessionToken(request);
            sessions.erase(key);
            HttpResponse resultResponse =
                response(200, "{\"ok\":true}");
            resultResponse.cookie =
                "bms_session=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0";
            return resultResponse;
        }

        int sessionStatus = 0;
        SessionManager* activeSession = session(request, sessionStatus);
        if (activeSession == nullptr) {
            return error(sessionStatus, "Your session has expired. Please sign in again.");
        }

        if (request.path == "/api/me") {
            return request.method == "GET"
                ? me(*activeSession)
                : error(405, "Method not allowed.");
        }
        if (request.path == "/api/transaction") {
            if (request.method != "POST") {
                return error(405, "Method not allowed.");
            }
            if (!roleMatches(*activeSession, infra::Role::CUSTOMER)) {
                return error(403, "Customer access is required.");
            }
            const auto* account =
                accounts.findByCustomer(activeSession->getUserId());
            if (account == nullptr) {
                return error(404, "No bank account is linked to this user.");
            }
            long long amountPaise = 0;
            if (!infra::InputValidator::parseAmountPaise(
                    get(request.form, "amount"), amountPaise)) {
                return error(400, "Enter a valid positive amount (maximum two decimal places).");
            }
            const std::string type = get(request.form, "type");
            TxnResult result;
            if (type == "DEPOSIT") {
                const std::string mode = get(request.form, "mode");
                if (mode != "CASH" && mode != "CHEQUE") {
                    return error(400, "Choose cash or cheque for the deposit.");
                }
                result = transactionService.deposit(
                    account->accountNo,
                    amountPaise,
                    mode == "CASH" ? DepositMode::CASH : DepositMode::CHEQUE
                );
            } else if (type == "WITHDRAWAL") {
                result = transactionService.withdraw(
                    account->accountNo, amountPaise
                );
            } else {
                return error(400, "Choose deposit or withdrawal.");
            }
            if (result.status != TxnStatus::OK) {
                return error(result.status == TxnStatus::STORAGE_FAILURE
                    ? 500 : 400, transactionError(result.status));
            }
            return response(200,
                "{\"ok\":true,\"txnId\":" + jsonString(result.txnId) +
                ",\"balancePaise\":" +
                jsonString(std::to_string(result.newBalancePaise)) + "}"
            );
        }
        if (request.path == "/api/change-pin") {
            if (request.method != "POST") {
                return error(405, "Method not allowed.");
            }
            const auto status = authService.changeCredentials(
                activeSession->getUserId(),
                get(request.form, "currentPin"),
                get(request.form, "newPin")
            );
            if (status != domain::Status::OK) {
                const int code = status == domain::Status::STORAGE_FAILURE
                    ? 500
                    : status == domain::Status::NOT_FOUND ? 404 : 400;
                return error(code, status == domain::Status::UNAUTHORIZED
                    ? "Current PIN is incorrect."
                    : status == domain::Status::INVALID_INPUT
                        ? "New PIN must contain 4 to 6 digits."
                        : "Could not change the PIN.");
            }
            return response(200, "{\"ok\":true}");
        }
        if (request.path == "/api/admin/status") {
            return request.method == "GET"
                ? adminStatus(*activeSession)
                : error(405, "Method not allowed.");
        }
        if (request.path == "/api/admin/create-account") {
            if (request.method != "POST") {
                return error(405, "Method not allowed.");
            }
            if (!roleMatches(*activeSession, infra::Role::ADMIN)) {
                return error(403, "Administrator access is required.");
            }
            CustomerInfo info;
            info.name = get(request.form, "name");
            info.phone = get(request.form, "phone");
            info.address = get(request.form, "address");
            info.initialPin = get(request.form, "initialPin");
            std::string accountNo;
            const auto status = accountService.createAccount(info, accountNo);
            if (status != domain::Status::OK) {
                return error(status == domain::Status::STORAGE_FAILURE
                    ? 500 : 400,
                    status == domain::Status::INVALID_INPUT
                        ? "Check the name, 10-digit phone number, address, and 4-6 digit PIN."
                        : "Could not create the customer account.");
            }
            if (!accounts.load().isOk() || !customerData.load()) {
                return error(500, "Account was created, but refreshing account data failed.");
            }
            const auto* account = accounts.find(accountNo);
            if (account == nullptr) {
                return error(500, "Account was created, but could not be reloaded.");
            }
            return response(200,
                "{\"ok\":true,\"customerId\":" +
                jsonString(account->customerId) +
                ",\"accountNo\":" + jsonString(accountNo) + "}"
            );
        }
        if (request.path == "/api/admin/unlock") {
            if (request.method != "POST") {
                return error(405, "Method not allowed.");
            }
            if (!roleMatches(*activeSession, infra::Role::ADMIN)) {
                return error(403, "Administrator access is required.");
            }
            if (!credentials.load()) {
                return error(500, "Could not load credential data.");
            }
            const auto status =
                unlockService.unlockAccount(*activeSession,
                                            get(request.form, "userId"));
            if (status != domain::Status::OK) {
                return error(status == domain::Status::NOT_FOUND ? 404 :
                    status == domain::Status::STORAGE_FAILURE ? 500 : 400,
                    status == domain::Status::NOT_FOUND
                        ? "Customer login ID not found."
                        : "Could not unlock this account.");
            }
            return response(200, "{\"ok\":true}");
        }
        return error(404, "Page or API route not found.");
    }

    bool serveClient(Socket client, unsigned short port) {
        HttpRequest request;
        int readError = 400;
        HttpResponse result;
        if (!readRequest(client, request, readError)) {
            result = error(readError, "Invalid or oversized HTTP request.");
        } else {
            result = handle(request, port);
        }

        std::ostringstream output;
        output << "HTTP/1.1 " << result.status << ' '
               << statusText(result.status) << "\r\n"
               << "Content-Type: " << result.contentType << "\r\n"
               << "Content-Length: " << result.body.size() << "\r\n"
               << "Connection: close\r\n"
               << "Cache-Control: no-store\r\n"
               << "X-Content-Type-Options: nosniff\r\n"
               << "Referrer-Policy: no-referrer\r\n"
               << "Content-Security-Policy: default-src 'self'; "
                  "style-src 'self'; script-src 'self'; "
                  "base-uri 'self'; frame-ancestors 'none'\r\n";
        if (!result.cookie.empty()) {
            output << "Set-Cookie: " << result.cookie << "\r\n";
        }
        output << "\r\n" << result.body;
        return sendAll(client, output.str());
    }
};

LocalWebServer::LocalWebServer(
    infra::LoginService& login,
    infra::CredentialStore& credentials,
    infra::AccountRepository& accounts,
    infra::TransactionRepository& transactions,
    infra::CustomerAccountStore& customerData,
    TransactionService& transactionService,
    AccountService& accountService,
    AccountUnlockService& unlockService,
    SystemStatusService& systemStatusService,
    AuthService& authService
)
    : impl_(std::make_unique<Impl>(
          login, credentials, accounts, transactions, customerData,
          transactionService, accountService, unlockService,
          systemStatusService, authService
      )) {}

LocalWebServer::~LocalWebServer() = default;

bool LocalWebServer::serve(unsigned short port) {
#ifdef _WIN32
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        std::cerr << "Could not initialize Windows networking.\n";
        return false;
    }
#endif

    Socket listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == kInvalidSocket) {
        std::cerr << "Could not create the web server socket.\n";
#ifdef _WIN32
        WSACleanup();
#endif
        return false;
    }

    int reuse = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR,
#ifdef _WIN32
               reinterpret_cast<const char*>(&reuse),
               static_cast<int>(sizeof(reuse))
#else
               &reuse, sizeof(reuse)
#endif
    );

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(listener, reinterpret_cast<sockaddr*>(&address),
               static_cast<int>(sizeof(address))) != 0 ||
        ::listen(listener, 8) != 0) {
        std::cerr << "Could not listen on http://127.0.0.1:"
                  << port << ". The port may already be in use.\n";
        closeSocket(listener);
#ifdef _WIN32
        WSACleanup();
#endif
        return false;
    }

    while (true) {
        Socket client = ::accept(listener, nullptr, nullptr);
        if (client == kInvalidSocket) {
            std::cerr << "Web server accept failed.\n";
            closeSocket(listener);
#ifdef _WIN32
            WSACleanup();
#endif
            return false;
        }
        impl_->serveClient(client, port);
        closeSocket(client);
    }
}

} // namespace bms::service
