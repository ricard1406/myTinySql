/// @file Output.hpp
/// @brief Turning statement results into console text or the HTML page.

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Catalog.hpp"
#include "Session.hpp"

namespace tinysql {

/// @brief Formats one result for the console.
/// @param result The result of one statement.
/// @return An echo of the statement, then a dashed header line and fixed-width rows,
///         a message, or an error; empty for a silent result.
std::string renderText(const StatementResult& result);

/// @brief Builds the full HTML page of the browser server.
///
/// The page has tools for the tables (show, describe, add a row), a box to type SQL into,
/// and one table per statement, with Edit and Delete buttons on each row that has an ID.
/// @param results The results to show, in order.
/// @param sql What the browser sent, or `std::nullopt` on a first visit (the box then starts with an example).
/// @param tables The tables for the tools; may be empty.
/// @return The complete page, from `<!doctype html>` to `</html>`.
std::string renderHtml(const std::vector<StatementResult>& results, const std::optional<std::string>& sql,
                       const std::vector<TableSchema>& tables);

}  // namespace tinysql
