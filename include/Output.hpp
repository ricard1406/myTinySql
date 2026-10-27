/// @file Output.hpp
/// @brief Turning statement results into console text or JSON for the browser page.

#pragma once

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

/// @brief Builds the JSON answer of `POST /api/sql`: one object per statement that has something to show.
///
/// Example: `{"results":[{"sql":"DELETE FROM EMPLOY WHERE ID = 3","ok":true,"message":"1 row deleted from EMPLOY",
/// "table":"EMPLOY"}]}`. A SELECT or DESCRIBE also has `"columns"` (name, width in the data file, and whether
/// the browser may edit the cell), `"rows"` (arrays of strings) and `"idColumn"` (index of the ID, or `null`).
/// @param results The results of a script, in order; silent ones (COLUMN, comments) are left out.
/// @return The JSON text.
std::string renderJson(const std::vector<StatementResult>& results);

/// @brief Builds the JSON answer of `GET /api/tables`: every table with its columns.
///
/// Example: `{"tables":[{"name":"EMPLOY","readOnly":false,"autoId":true,"columns":[{"name":"ID","width":10},...]}]}`.
/// `readOnly` is true for SYSTEM_TABLE, which the page must not change.
/// @param tables The tables, from Catalog::tables().
/// @return The JSON text.
std::string renderTablesJson(const std::vector<TableSchema>& tables);

}  // namespace tinysql
