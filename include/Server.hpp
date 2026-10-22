#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tinysql {

/// The browser server: GET /?subject=<url-encoded SQL> answers an HTML page with the results.
struct ServerConfig {
    std::uint16_t port = 8080;
    std::filesystem::path dataDir;
    std::vector<std::string> allowedClients;  // IP prefixes such as "192.168."; empty = everyone
};

/// Serves connections until the process is stopped: each in its own thread, SQL one request at a time.
void runServer(const ServerConfig& config);

/// Decodes %XX escapes and '+' (space) from a URL.
std::string urlDecode(std::string_view text);

/// The SQL in the `subject` parameter of an HTTP request, or std::nullopt if there is none.
std::optional<std::string> sqlFromRequest(std::string_view request);

}  // namespace tinysql
