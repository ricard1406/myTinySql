/// @file Server.cpp
/// @brief The HTTP server: sockets, one thread per connection, and request parsing.

#include "Server.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <format>
#include <iostream>
#include <mutex>
#include <system_error>
#include <thread>
#include <utility>

#include "Catalog.hpp"
#include "Output.hpp"
#include "Session.hpp"
#include "Util.hpp"

namespace tinysql {

namespace {

/// @brief The largest request read, in bytes; the rest is ignored.
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

/// @brief Reads an HTTP request's request line and headers.
/// @param fd The connection.
/// @return Everything up to the blank line after the headers (at most kMaxRequestSize bytes);
///         empty if the client sent nothing before the idle timeout.
std::string readRequest(int fd)
{
    std::string request;
    char buffer[4096];
    while (request.size() < kMaxRequestSize && request.find("\r\n\r\n") == std::string::npos) {
        const ssize_t n = ::recv(fd, buffer, sizeof buffer, 0);
        if (n <= 0) {
            break;
        }
        request.append(buffer, static_cast<std::size_t>(n));
    }
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

/// @brief Builds a complete HTTP response with an HTML body.
/// @param status The status line's code and text, e.g. "200 OK".
/// @param body The HTML.
/// @return The response, headers included.
std::string httpResponse(std::string_view status, std::string_view body)
{
    return std::format("HTTP/1.1 {}\r\nContent-Type: text/html; charset=utf-8\r\n"
                       "Content-Length: {}\r\nConnection: close\r\n\r\n{}",
                       status, body.size(), body);
}

/// @brief Extracts the target of a GET request.
/// @param request The raw request.
/// @return For `GET /path?query HTTP/1.1`, the text `/path?query`; empty if the request is not a GET.
std::string_view requestTarget(std::string_view request)
{
    const std::string_view line = request.substr(0, request.find("\r\n"));
    if (!line.starts_with("GET ")) {
        return {};
    }
    std::string_view target = line.substr(4);
    return target.substr(0, target.find(' '));
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

/// @brief Runs the SQL of one request, logs it, and builds the page to send back.
///
/// Call it only while holding workMutex.
/// @param config The server settings.
/// @param request The raw request.
/// @param client The client's address, for the log.
/// @param target The request target, for the log.
/// @return The HTML page.
std::string runRequest(const ServerConfig& config, const std::string& request, const std::string& client,
                       std::string_view target);

/// @brief Serves one connection: reads the request, runs it, and sends the answer.
///
/// A connection that sends nothing is closed without an answer; a non-GET request gets 405,
/// and any path but `/` gets 404.
/// @param config The server settings.
/// @param fd The connection.
/// @param client The client's address, for the log.
void handleClient(const ServerConfig& config, int fd, const std::string& client)
{
    const std::string request = readRequest(fd);
    if (trim(request).empty()) {
        // An idle (preconnected) connection that never sent a request: close it without
        // answering. Had we answered, the browser could later read that answer as the reply
        // to the request it sends on this connection.
        return;
    }

    const std::string_view target = requestTarget(request);
    if (target.empty()) {
        sendAll(fd, httpResponse("405 Method Not Allowed", "<p>Only GET requests are supported.</p>"));
        return;
    }
    if (target.substr(0, target.find('?')) != "/") {
        sendAll(fd, httpResponse("404 Not Found", "<p>Not found.</p>"));
        return;
    }

    std::string body;
    {
        const std::scoped_lock lock(workMutex);  // one request at a time from here on
        body = runRequest(config, request, client, target);
    }  // the lock is released here, before the (possibly slow) send
    sendAll(fd, httpResponse("200 OK", body));
}

std::string runRequest(const ServerConfig& config, const std::string& request, const std::string& client,
                       std::string_view target)
{
    std::cout << std::format("[{}] GET {}\n", client, target);

    const std::optional<std::string> sql = sqlFromRequest(request);
    Session session(config.dataDir);
    const auto results = sql ? session.executeScript(*sql) : std::vector<StatementResult>{};
    for (const StatementResult& result : results) {
        std::cout << renderText(result);
    }
    std::cout << std::flush;

    // The table list feeds the page's tools; without a readable catalogue the page simply has none.
    std::vector<TableSchema> tables;
    try {
        tables = Catalog::load(config.dataDir).tables();
    } catch (const SqlError&) {
    }
    return renderHtml(results, sql, tables);
}

}  // namespace

std::string urlDecode(std::string_view text)
{
    std::string result;
    result.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '+') {
            result += ' ';
        } else if (c == '%' && i + 2 < text.size()) {
            unsigned value = 0;
            const char* begin = text.data() + i + 1;
            auto [ptr, ec] = std::from_chars(begin, begin + 2, value, 16);
            if (ec == std::errc{} && ptr == begin + 2) {
                result += static_cast<char>(value);
                i += 2;
            } else {
                result += c;
            }
        } else {
            result += c;
        }
    }
    return result;
}

std::optional<std::string> sqlFromRequest(std::string_view request)
{
    const std::string_view target = requestTarget(request);
    if (target.empty()) {
        return std::nullopt;
    }

    // GET /?subject=SELECT+*+FROM+EMPLOY%3B...
    const std::size_t question = target.find('?');
    if (question == std::string_view::npos) {
        return std::nullopt;
    }
    std::string_view query = target.substr(question + 1);
    while (!query.empty()) {
        const std::size_t amp = query.find('&');
        const std::string_view pair = query.substr(0, amp);
        if (pair.starts_with("subject=")) {
            return urlDecode(pair.substr(8));
        }
        query = amp == std::string_view::npos ? std::string_view{} : query.substr(amp + 1);
    }
    return std::nullopt;
}

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
