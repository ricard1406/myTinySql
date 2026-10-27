/// @file Server.cpp
/// @brief The HTTP server: the page's files, a small JSON API, and one thread per connection.

#include "Server.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <format>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <system_error>
#include <thread>
#include <utility>

#include "Catalog.hpp"
#include "Output.hpp"
#include "Session.hpp"
#include "Util.hpp"

namespace tinysql {

namespace {

/// @brief The largest request read (headers and body), in bytes; the rest is ignored.
constexpr std::size_t kMaxRequestSize = 64 * 1024;

/// @brief How long a connection may stay silent before it is closed, in seconds.
///
/// Browsers open connections early ("preconnect") and may send nothing on them for several seconds.
constexpr int kIdleSeconds = 30;

/// @brief Lets only one request at a time run SQL and write to the console.
///
/// Each connection is read in its own thread; this lock keeps the table files and the log from being mixed up.
std::mutex workMutex;

/// @brief Owns a socket descriptor and closes it when it goes out of scope (RAII).
class Socket {
public:
    /// @brief Takes ownership of a descriptor.
    /// @param fd The descriptor, or -1 for none.
    explicit Socket(int fd) : fd_(fd) {}

    /// @brief Closes the descriptor, if there is one.
    ~Socket()
    {
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }

    Socket(const Socket&) = delete;             ///< Not copyable: two copies would close the same descriptor.
    Socket& operator=(const Socket&) = delete;  ///< Not copyable.

    /// @brief Takes over another Socket's descriptor, here to hand a connection to its thread.
    /// @param other The Socket to take from; it is left empty, so the descriptor is closed exactly once.
    Socket(Socket&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}
    Socket& operator=(Socket&&) = delete;  ///< Not needed: a Socket is only ever moved into a new Socket, never assigned.

    /// @brief The descriptor, for system calls.
    /// @return The descriptor, or -1 for none.
    int fd() const { return fd_; }

private:
    int fd_;  ///< The descriptor, or -1 for none.
};

/// @brief Throws the error of the last failed system call.
/// @param what What was being done, e.g. "bind to port 8080".
/// @throws std::system_error Always, with the current `errno`.
[[noreturn]] void throwSystemError(std::string_view what)
{
    throw std::system_error(errno, std::generic_category(), std::string(what));
}

/// @brief One HTTP request, split into the parts the server uses.
struct Request {
    std::string method;  ///< E.g. `GET` or `POST`; empty if the request could not be read.
    std::string path;    ///< E.g. `/api/sql`; any `?query` part is removed.
    std::string body;    ///< The body of a POST (the SQL for `/api/sql`); empty for a GET.
};

/// @brief Finds a header's value in the raw request, ignoring the case of its name.
/// @param head The request line and headers.
/// @param name The header name, e.g. "content-length".
/// @return The value without surrounding spaces, or an empty view if the header is missing.
std::string_view headerValue(std::string_view head, std::string_view name)
{
    std::size_t lineStart = head.find("\r\n");
    while (lineStart != std::string_view::npos && lineStart + 2 < head.size()) {
        lineStart += 2;
        const std::size_t lineEnd = head.find("\r\n", lineStart);
        const std::string_view line = head.substr(lineStart, lineEnd - lineStart);
        const std::size_t colon = line.find(':');
        if (colon != std::string_view::npos && equalsIgnoreCase(trim(line.substr(0, colon)), name)) {
            return trim(line.substr(colon + 1));
        }
        lineStart = lineEnd;
    }
    return {};
}

/// @brief Reads one HTTP request: the request line, the headers and, if there is one, the body.
/// @param fd The connection.
/// @return The request. Its method is empty if the client sent nothing before the idle timeout,
///         and the body is cut at kMaxRequestSize bytes.
Request readRequest(int fd)
{
    std::string raw;
    char buffer[4096];
    std::size_t headEnd = std::string::npos;
    std::size_t bodyLength = 0;
    while (raw.size() < kMaxRequestSize) {
        if (headEnd == std::string::npos && (headEnd = raw.find("\r\n\r\n")) != std::string::npos) {
            // The headers are complete: from now on, read until the body has Content-Length bytes.
            bodyLength = parseInteger(headerValue(std::string_view(raw).substr(0, headEnd), "content-length")).value_or(0);
        }
        if (headEnd != std::string::npos && raw.size() >= headEnd + 4 + bodyLength) {
            break;
        }
        const ssize_t n = ::recv(fd, buffer, sizeof buffer, 0);
        if (n <= 0) {
            break;
        }
        raw.append(buffer, static_cast<std::size_t>(n));
    }

    Request request;
    if (headEnd == std::string::npos) {
        return request;  // nothing, or not a complete request
    }
    // Request line: "POST /api/sql HTTP/1.1"
    const std::string_view line = std::string_view(raw).substr(0, raw.find("\r\n"));
    const std::size_t space1 = line.find(' ');
    const std::size_t space2 = line.find(' ', space1 + 1);
    if (space1 == std::string_view::npos || space2 == std::string_view::npos) {
        return request;
    }
    request.method = std::string(line.substr(0, space1));
    const std::string_view target = line.substr(space1 + 1, space2 - space1 - 1);
    request.path = std::string(target.substr(0, target.find('?')));
    request.body = raw.substr(headEnd + 4, bodyLength);
    return request;
}

/// @brief Sends all of `data`, retrying until it is written or the client goes away.
/// @param fd The connection.
/// @param data The bytes to send.
void sendAll(int fd, std::string_view data)
{
    while (!data.empty()) {
        const ssize_t n = ::send(fd, data.data(), data.size(), MSG_NOSIGNAL);
        if (n <= 0) {
            return;  // client went away
        }
        data.remove_prefix(static_cast<std::size_t>(n));
    }
}

/// @brief Builds a complete HTTP response.
/// @param status The status line's code and text, e.g. "200 OK".
/// @param contentType The type of the body, e.g. "application/json".
/// @param body The body.
/// @return The response, headers included. `no-cache` makes the browser ask again each time,
///         so a change to a file in `web/` shows up on the next reload.
std::string httpResponse(std::string_view status, std::string_view contentType, std::string_view body)
{
    return std::format("HTTP/1.1 {}\r\nContent-Type: {}\r\nContent-Length: {}\r\n"
                       "Cache-Control: no-cache\r\nConnection: close\r\n\r\n{}",
                       status, contentType, body.size(), body);
}

/// @brief Builds a JSON error answer, e.g. `{"error":"Not found"}`.
/// @param status The status line's code and text, e.g. "404 Not Found".
/// @param message The error message.
/// @return The response.
std::string errorResponse(std::string_view status, std::string_view message)
{
    return httpResponse(status, "application/json", std::format("{{\"error\":\"{}\"}}", message));
}

/// @brief The content type of a file of the page, from its extension.
/// @param name The file name, e.g. "app.js".
/// @return The type to send in the Content-Type header.
std::string_view contentTypeFor(std::string_view name)
{
    if (name.ends_with(".html")) return "text/html; charset=utf-8";
    if (name.ends_with(".css")) return "text/css; charset=utf-8";
    if (name.ends_with(".js")) return "text/javascript; charset=utf-8";
    if (name.ends_with(".svg")) return "image/svg+xml";
    if (name.ends_with(".ico")) return "image/x-icon";
    return "application/octet-stream";
}

/// @brief Tells whether a requested file name is safe to serve from the `web/` directory.
/// @param name The name, without the leading `/`.
/// @return True for plain names such as `app.js`: letters, digits, `.`, `-` and `_`, not starting with `.`.
///         This rules out `..` and `/`, so no request can reach a file outside `web/`.
bool isSafeFileName(std::string_view name)
{
    return !name.empty() && name.front() != '.' && std::ranges::all_of(name, [](unsigned char c) {
        return std::isalnum(c) || c == '.' || c == '-' || c == '_';
    });
}

/// @brief Serves one file of the page from the `web/` directory.
/// @param config The server settings.
/// @param name The file name; `index.html` for the page itself.
/// @return The response: the file, or 404 if the name is not safe or the file does not exist.
std::string serveFile(const ServerConfig& config, std::string_view name)
{
    if (!isSafeFileName(name)) {
        return errorResponse("404 Not Found", "Not found");
    }
    std::ifstream in(config.webDir / name, std::ios::binary);
    if (!in) {
        return errorResponse("404 Not Found", "Not found");
    }
    std::stringstream content;
    content << in.rdbuf();
    return httpResponse("200 OK", contentTypeFor(name), content.str());
}

/// @brief Runs the SQL of a `POST /api/sql` request, logs it, and builds the JSON answer.
///
/// Call it only while holding workMutex.
/// @param config The server settings.
/// @param sql The statements, as sent in the request body.
/// @return The response.
std::string runSql(const ServerConfig& config, std::string_view sql)
{
    Session session(config.dataDir);
    const auto results = session.executeScript(sql);
    for (const StatementResult& result : results) {
        std::cout << renderText(result);
    }
    std::cout << std::flush;
    return httpResponse("200 OK", "application/json", renderJson(results));
}

/// @brief Builds the JSON answer of `GET /api/tables`. Call it only while holding workMutex.
/// @param config The server settings.
/// @return The response, or a 500 error if SYSTEM_TABLE cannot be read.
std::string listTables(const ServerConfig& config)
{
    try {
        return httpResponse("200 OK", "application/json", renderTablesJson(Catalog::load(config.dataDir).tables()));
    } catch (const SqlError& e) {
        return errorResponse("500 Internal Server Error", "cannot read the table catalogue");
    }
}

/// @brief Formats a client's IPv4 address.
/// @param address The address returned by `accept()`.
/// @return The address as text, e.g. "192.168.1.10".
std::string clientAddress(const sockaddr_in& address)
{
    char text[INET_ADDRSTRLEN] = {};
    ::inet_ntop(AF_INET, &address.sin_addr, text, sizeof text);
    return text;
}

/// @brief Serves one connection: reads the request, answers it, and closes.
///
/// The routes:
/// - `GET /` and `GET /<file>`: the page, from the `web/` directory;
/// - `GET /api/tables`: the tables and their columns, as JSON;
/// - `POST /api/sql`: runs the SQL in the body, answers with the results as JSON.
///
/// A connection that sends nothing is closed without an answer; a POST elsewhere gets 404, other methods 405.
/// @param config The server settings.
/// @param fd The connection.
/// @param client The client's address, for the log.
void handleClient(const ServerConfig& config, int fd, const std::string& client)
{
    const Request request = readRequest(fd);
    if (request.method.empty()) {
        // An idle (preconnected) connection that never sent a request: close it without
        // answering. Had we answered, the browser could later read that answer as the reply
        // to the request it sends on this connection.
        return;
    }

    std::string response;
    if (request.method == "GET" && request.path == "/api/tables") {
        const std::scoped_lock lock(workMutex);
        response = listTables(config);
    } else if (request.method == "POST" && request.path == "/api/sql") {
        const std::scoped_lock lock(workMutex);  // one request at a time runs SQL
        std::cout << std::format("[{}] POST /api/sql\n", client);
        response = runSql(config, request.body);
    } else if (request.method == "GET") {
        response = serveFile(config, request.path == "/" ? "index.html" : std::string_view(request.path).substr(1));
    } else if (request.method == "POST") {
        response = errorResponse("404 Not Found", "Not found");
    } else {
        response = errorResponse("405 Method Not Allowed", "Only GET and POST are supported");
    }
    sendAll(fd, response);
}

}  // namespace

void runServer(const ServerConfig& config)
{
    Socket server(::socket(AF_INET, SOCK_STREAM, 0));
    if (server.fd() < 0) {
        throwSystemError("socket");
    }
    const int yes = 1;
    ::setsockopt(server.fd(), SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(config.port);
    if (::bind(server.fd(), reinterpret_cast<sockaddr*>(&address), sizeof address) < 0) {
        throwSystemError(std::format("bind to port {}", config.port));
    }
    if (::listen(server.fd(), 10) < 0) {
        throwSystemError("listen");
    }
    std::cout << std::format("HTTP server listening on port {}\n", config.port) << std::flush;

    while (true) {
        sockaddr_in clientAddr{};
        socklen_t length = sizeof clientAddr;
        Socket client(::accept(server.fd(), reinterpret_cast<sockaddr*>(&clientAddr), &length));
        if (client.fd() < 0) {
            if (errno == EINTR) {
                continue;
            }
            throwSystemError("accept");
        }

        const timeval timeout{.tv_sec = kIdleSeconds, .tv_usec = 0};
        ::setsockopt(client.fd(), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);

        const std::string ip = clientAddress(clientAddr);
        const bool allowed =
            config.allowedClients.empty() ||
            std::ranges::any_of(config.allowedClients, [&](const std::string& prefix) { return ip.starts_with(prefix); });
        if (!allowed) {
            const std::scoped_lock lock(workMutex);
            std::cout << std::format("[{}] rejected: not in the allowed clients\n", ip) << std::flush;
            continue;
        }

        // Serve the connection in its own thread, so a slow or silent client never makes the
        // others wait. The thread owns the socket (moved in) and closes it when it ends.
        std::thread([config, ip, socket = std::move(client)] {
            try {
                handleClient(config, socket.fd(), ip);
            } catch (const std::exception& e) {
                const std::scoped_lock lock(workMutex);
                std::cerr << std::format("[{}] error: {}\n", ip, e.what());
            }
        }).detach();
    }
}

}  // namespace tinysql
