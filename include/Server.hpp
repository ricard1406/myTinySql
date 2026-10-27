/// @file Server.hpp
/// @brief The small HTTP server behind `--http`: the page and its JSON API.

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace tinysql {

/// @brief Settings of the browser server.
struct ServerConfig {
    std::uint16_t port = 8080;                ///< TCP port to listen on.
    std::filesystem::path dataDir;            ///< The directory holding the tables.
    std::filesystem::path webDir;             ///< The directory holding the page: index.html, style.css, app.js.
    std::vector<std::string> allowedClients;  ///< IP prefixes such as `192.168.`; empty means everyone.
};

/// @brief Serves connections until the process is stopped.
///
/// It serves the page's files from ServerConfig::webDir and a small JSON API: `GET /api/tables`
/// lists the tables, `POST /api/sql` runs the SQL in the request body. Each connection is served
/// in its own thread; the SQL itself runs one request at a time.
/// @param config The server settings.
/// @throws std::system_error If the port cannot be opened (for example, it is already in use).
void runServer(const ServerConfig& config);

}  // namespace tinysql
