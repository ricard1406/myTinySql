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
#include <system_error>

#include "Catalog.hpp"
#include "Output.hpp"
#include "Session.hpp"
#include "Util.hpp"

namespace tinysql {

namespace {

constexpr std::size_t kMaxRequestSize = 64 * 1024;

/// Owns a socket descriptor and closes it when it goes out of scope.
class Socket {
public:
    explicit Socket(int fd) : fd_(fd) {}
    ~Socket()
    {
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;

    int fd() const { return fd_; }

private:
    int fd_;
};

[[noreturn]] void throwSystemError(std::string_view what)
{
    throw std::system_error(errno, std::generic_category(), std::string(what));
}

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

std::string httpResponse(std::string_view status, std::string_view body)
{
    return std::format("HTTP/1.1 {}\r\nContent-Type: text/html; charset=utf-8\r\n"
                       "Content-Length: {}\r\nConnection: close\r\n\r\n{}",
                       status, body.size(), body);
}

/// "GET /path?query HTTP/1.1" -> "/path?query"; empty if the request is not a GET.
std::string_view requestTarget(std::string_view request)
{
    const std::string_view line = request.substr(0, request.find("\r\n"));
    if (!line.starts_with("GET ")) {
        return {};
    }
    std::string_view target = line.substr(4);
    return target.substr(0, target.find(' '));
}

std::string clientAddress(const sockaddr_in& address)
{
    char text[INET_ADDRSTRLEN] = {};
    ::inet_ntop(AF_INET, &address.sin_addr, text, sizeof text);
    return text;
}

void handleClient(const ServerConfig& config, int fd, const std::string& client)
{
    const std::string request = readRequest(fd);
    const std::string_view target = requestTarget(request);
    std::cout << std::format("[{}] GET {}\n", client, target) << std::flush;

    if (target.empty()) {
        sendAll(fd, httpResponse("400 Bad Request", "<p>Only GET requests are supported.</p>"));
        return;
    }
    if (config.mode == ServerMode::Http && target.substr(0, target.find('?')) != "/") {
        sendAll(fd, httpResponse("404 Not Found", "<p>Not found.</p>"));
        return;
    }

    const std::optional<std::string> sql = sqlFromRequest(config.mode, request);
    Session session(config.dataDir);
    const auto results = sql ? session.executeScript(*sql) : std::vector<StatementResult>{};
    for (const StatementResult& result : results) {
        std::cout << renderText(result);
    }
    std::cout << std::flush;

    std::string body;
    if (config.mode == ServerMode::Http) {
        // The table list feeds the page's tools; without a readable catalogue the page simply has none.
        std::vector<TableSchema> tables;
        try {
            tables = Catalog::load(config.dataDir).tables();
        } catch (const SqlError&) {
        }
        body = renderHtml(results, sql, tables);
    } else {
        body = renderAndroid(results);
    }
    sendAll(fd, httpResponse("200 OK", body));
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

std::optional<std::string> sqlFromRequest(ServerMode mode, std::string_view request)
{
    const std::string_view target = requestTarget(request);
    if (target.empty()) {
        return std::nullopt;
    }

    if (mode == ServerMode::Android) {
        // GET /SELECT+*+FROM+EMPLOY;SELECT+...   ('+' = space, ';' separates statements)
        std::string sql = urlDecode(target.substr(1));
        if (sql.empty()) {
            return std::nullopt;
        }
        return sql;
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
    std::cout << std::format("{} server listening on port {}\n",
                             config.mode == ServerMode::Http ? "HTTP" : "Android", config.port)
              << std::flush;

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

        const timeval timeout{.tv_sec = 5, .tv_usec = 0};
        ::setsockopt(client.fd(), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);

        const std::string ip = clientAddress(clientAddr);
        const bool allowed =
            config.allowedClients.empty() ||
            std::ranges::any_of(config.allowedClients, [&](const std::string& prefix) { return ip.starts_with(prefix); });
        if (!allowed) {
            std::cout << std::format("[{}] rejected: not in the allowed clients\n", ip) << std::flush;
            continue;
        }

        try {
            handleClient(config, client.fd(), ip);
        } catch (const std::exception& e) {
            std::cerr << std::format("[{}] error: {}\n", ip, e.what());
        }
    }
}

}  // namespace tinysql
