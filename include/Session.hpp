/// @file Session.hpp
/// @brief Running statements, and the results they produce.

#pragma once

#include <cstddef>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Statement.hpp"

namespace tinysql {

/// @brief The rows produced by a SELECT or DESCRIBE. All per-column vectors have the same length.
struct ResultSet {
    std::vector<std::string> headers;        ///< Column titles.
    std::vector<std::size_t> widths;         ///< Display width of each column in console output.
    std::vector<bool> literal;               ///< True for a `'literal'` SELECT item.
    std::vector<std::size_t> storageWidths;  ///< Width of each column in the data file (0 for a literal).
    std::vector<std::vector<std::string>> rows;  ///< The values, one inner vector per row.
    std::optional<std::size_t> idColumn;     ///< Output column holding the row ID, if rows can be edited and deleted by ID.
};

/// @brief What running one statement produced: a table, a message, or an error.
struct StatementResult {
    std::string sql;                 ///< The statement as it was run.
    std::string tableName;           ///< The table the statement worked on, if any.
    std::optional<ResultSet> table;  ///< The rows, for SELECT and DESCRIBE.
    std::string message;             ///< E.g. "1 row inserted ...", or the error text.
    bool failed = false;             ///< True if the statement failed; #message holds the error.
    bool silent = false;             ///< True for COLUMN and comments: nothing to show.
};

/// @brief Runs statements against the tables in one data directory.
///
/// `COLUMN ... FORMAT` settings last for the whole session. Every statement reloads the catalogue,
/// so changes to the files are seen at once.
class Session {
public:
    /// @brief Starts a session.
    /// @param dataDir The directory holding `SYSTEM_TABLE` and the data files.
    explicit Session(std::filesystem::path dataDir);

    /// @brief Parses and runs one statement.
    /// @param sql The text of one statement.
    /// @return The result. Errors never escape: a failed statement gives a result with
    ///         StatementResult::failed set and the error in StatementResult::message.
    StatementResult execute(std::string_view sql);

    /// @brief Runs every statement of a script, in order.
    /// @param script Statements one per line, or separated by `;`.
    /// @return One result per statement; a failed statement does not stop the ones after it.
    std::vector<StatementResult> executeScript(std::string_view script);

private:
    /// @brief Runs a SELECT.
    /// @param stmt The statement.
    /// @return The rows found.
    /// @throws SqlError On an unknown table or column, or a missing data file.
    StatementResult run(const SelectStmt& stmt);

    /// @brief Runs an INSERT, assigning the next ID when it is left out.
    /// @param stmt The statement.
    /// @return A message with the new row's ID.
    /// @throws SqlError On a wrong number of values, a duplicate ID, or a value too long for its column.
    StatementResult run(const InsertStmt& stmt);

    /// @brief Runs an UPDATE.
    /// @param stmt The statement.
    /// @return A message saying how many rows changed.
    /// @throws SqlError On an unknown column, an attempt to change the ID or SYSTEM_TABLE,
    ///         or a value too long for its column.
    StatementResult run(const UpdateStmt& stmt);

    /// @brief Runs a DELETE.
    /// @param stmt The statement.
    /// @return A message saying how many rows were removed.
    /// @throws SqlError On an unknown column or an attempt to change SYSTEM_TABLE.
    StatementResult run(const DeleteStmt& stmt);

    /// @brief Runs a CREATE TABLE: adds one SYSTEM_TABLE line per column and an empty data file.
    /// @param stmt The statement.
    /// @return A message with the number of columns and the row length.
    /// @throws SqlError On an invalid or duplicate name, an ID column that is not the first one,
    ///         a row too long for SYSTEM_TABLE, or a data file that already exists.
    StatementResult run(const CreateTableStmt& stmt);

    /// @brief Remembers a column's display width for the rest of the session.
    /// @param stmt The statement.
    /// @return A silent result.
    StatementResult run(const ColumnFormatStmt& stmt);

    /// @brief Lists the tables, or one table's columns.
    /// @param stmt The statement.
    /// @return The list as a ResultSet.
    /// @throws SqlError On an unknown table.
    StatementResult run(const DescribeStmt& stmt);

    /// @brief Does nothing: comments have no effect.
    /// @return A silent result.
    StatementResult run(const CommentStmt&);

    std::filesystem::path dataDir_;                     ///< Where the tables are.
    std::map<std::string, std::size_t> displayWidths_;  ///< Upper-case column name to display width.
};

}  // namespace tinysql
