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

/// The rows produced by a SELECT or DESCRIBE.
struct ResultSet {
    std::vector<std::string> headers;
    std::vector<std::size_t> widths;    // display width of each column in text output
    std::vector<bool> literal;          // true for a 'literal' SELECT item
    std::vector<std::size_t> storageWidths;  // width of each column in the data file (0 for a literal)
    std::vector<std::vector<std::string>> rows;
    bool valueTags = false;             // ANDROID-LIST: Android output uses <VALUE> tags
    std::optional<std::size_t> idColumn;  // output column holding the row ID, if rows can be edited and deleted by ID
};

struct StatementResult {
    std::string sql;
    std::string tableName;  // the table the statement worked on, if any
    std::optional<ResultSet> table;
    std::string message;  // e.g. "1 row inserted ...", or the error text
    bool failed = false;
    bool silent = false;  // COLUMN and comments: nothing to show
};

/// Runs statements against the tables in one data directory.
/// COLUMN ... FORMAT settings last for the whole session.
class Session {
public:
    explicit Session(std::filesystem::path dataDir);

    StatementResult execute(std::string_view sql);
    std::vector<StatementResult> executeScript(std::string_view script);

private:
    StatementResult run(const SelectStmt& stmt);
    StatementResult run(const InsertStmt& stmt);
    StatementResult run(const UpdateStmt& stmt);
    StatementResult run(const DeleteStmt& stmt);
    StatementResult run(const ColumnFormatStmt& stmt);
    StatementResult run(const DescribeStmt& stmt);
    StatementResult run(const CommentStmt& stmt);

    std::filesystem::path dataDir_;
    std::map<std::string, std::size_t> displayWidths_;  // upper-case column name -> width
};

}  // namespace tinysql
