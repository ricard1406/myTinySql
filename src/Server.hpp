#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tinysql {

enum class ServerMode {
    Http,     // browser: GET /?subject=<url-encoded SQL>, answers an HTML page with a form
    Android,  // Android app: GET /<SQL>, answers rows as <COLUMN>value</COLUMN> tags
};

struct ServerConfig {
    ServerMode mode = ServerMode::Http;
    std::uint16_t port = 8080;
    std::filesystem::path dataDir;
    std::vector<std::string> allowedClients;  // IP prefixes such as "151."; empty = everyone
};

/// Serves one request at a time until the process is stopped.
void runServer(const ServerConfig& config);

/// Decodes %XX escapes and '+' (space) from a URL.
std::string urlDecode(std::string_view text);

/// The SQL carried by an HTTP request, or std::nullopt if it carries none.
std::optional<std::string> sqlFromRequest(ServerMode mode, std::string_view request);

}  // namespace tinysql
