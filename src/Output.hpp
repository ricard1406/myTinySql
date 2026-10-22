#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Catalog.hpp"
#include "Session.hpp"

namespace tinysql {

/// Console output: an echo of the statement, a dashed header line, fixed-width rows.
std::string renderText(const StatementResult& result);

/// A full HTML page: tools for the tables (show, describe, add a row), a box to type SQL into,
/// then one table per statement, with Edit and Delete buttons on each row that has an ID.
/// `sql` is what the browser sent; std::nullopt on a first visit (the box starts with an example).
std::string renderHtml(const std::vector<StatementResult>& results, const std::optional<std::string>& sql,
                       const std::vector<TableSchema>& tables);

}  // namespace tinysql
