/// @file Server.hpp
/// @brief The small HTTP server behind `--http`.

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tinysql {

/// @brief Settings of the browser server, which answers `GET /?subject=<url-encoded SQL>` with an HTML page.
struct ServerConfig {
    std::uint16_t port = 8080;                ///< TCP port to listen on.
    std::filesystem::path dataDir;            ///< The directory holding the tables.
    std::vector<std::string> allowedClients;  ///< IP prefixes such as `192.168.`; empty means everyone.
};

/// @brief Serves connections until the process is stopped.
///
/// Each connection is served in its own thread; the SQL itself runs one request at a time.
/// @param config The server settings.
/// @throws std::system_error If the port cannot be opened (for example, it is already in use).
void runServer(const ServerConfig& config);

/// @brief Decodes a URL-encoded string.
/// @param text Text with `%XX` escapes and `+` for spaces.
/// @return The decoded text.
std::string urlDecode(std::string_view text);

/// @brief Extracts the SQL from an HTTP request.
/// @param request The raw request, starting with its request line.
/// @return The decoded `subject` parameter, or `std::nullopt` if the request is not a GET or has none.
std::optional<std::string> sqlFromRequest(std::string_view request);

}  // namespace tinysql
