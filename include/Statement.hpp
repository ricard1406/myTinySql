/// @file Statement.hpp
/// @brief The parsed form of every statement, and the functions that build it from text.

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace tinysql {

/// @brief One entry of a SELECT list: `*`, a column name, or a `'quoted literal'` printed on every row.
struct SelectItem {
    /// @brief Which kind of entry this is.
    enum class Kind {
        Star,     ///< `*`: every column of the table.
        Column,   ///< A column name.
        Literal,  ///< A `'quoted text'`, printed as-is on every row.
    };
    Kind kind;         ///< Which kind of entry this is.
    std::string text;  ///< The column name or the literal text (`*` for Kind::Star).
};

/// @brief A `WHERE <column> <op> <value>` test.
struct Condition {
    std::string column;  ///< The column to test.
    std::string op;      ///< One of `=` `!=` `<>` `<` `<=` `>` `>=`.
    std::string value;   ///< The value to compare with; compared as a number when both sides are integers.
};

/// @brief `SELECT <items> FROM <table> [WHERE <condition>]`.
struct SelectStmt {
    std::vector<SelectItem> items;   ///< What to show, in order.
    std::string table;               ///< The table to read.
    std::optional<Condition> where;  ///< The optional WHERE test.
};

/// @brief `INSERT INTO <table> VALUES (<values>)`.
struct InsertStmt {
    std::string table;                ///< The table to add a row to.
    std::vector<std::string> values;  ///< The values, without the ID when it is assigned automatically.
};

/// @brief `DELETE FROM <table> WHERE <condition>`. The WHERE is required, so a table is never emptied by accident.
struct DeleteStmt {
    std::string table;  ///< The table to remove rows from.
    Condition where;    ///< Which rows to remove.
};

/// @brief One `<column> = <value>` of an UPDATE's SET list.
struct Assignment {
    std::string column;  ///< The column to change.
    std::string value;   ///< Its new value.
};

/// @brief `UPDATE <table> SET <col> = <value>[, ...] WHERE <condition>`. The WHERE is required, as for DELETE.
struct UpdateStmt {
    std::string table;                    ///< The table to change.
    std::vector<Assignment> assignments;  ///< The columns to set and their new values.
    Condition where;                      ///< Which rows to change.
};

/// @brief `COLUMN <name> FORMAT A<width>`: the console display width of a column.
struct ColumnFormatStmt {
    std::string column;  ///< The column name.
    std::size_t width;   ///< Display width in characters.
};

/// @brief `DESCRIBE` lists the tables; `DESCRIBE <table>` lists that table's columns.
struct DescribeStmt {
    std::optional<std::string> table;  ///< The table to describe, or empty to list all tables.
};

/// @brief `REM ...`, `-- ...`, or an empty line: nothing to run.
struct CommentStmt {};

/// @brief Any parsed statement: exactly one of the statement types above.
///
/// Session::execute() uses `std::visit` to call the matching `run()` overload.
using Statement =
    std::variant<SelectStmt, InsertStmt, UpdateStmt, DeleteStmt, ColumnFormatStmt, DescribeStmt, CommentStmt>;

/// @brief Parses one statement.
/// @param sql The text of one statement; a trailing `;` is allowed.
/// @return The parsed statement.
/// @throws SqlError On a syntax error; the message says what was expected and what was found.
Statement parseStatement(std::string_view sql);

/// @brief Splits a script into statements.
/// @param script Any number of statements: one per line, and `;` outside quotes also ends one.
/// @return The statements in order, trimmed; comment lines and empty pieces are left out.
std::vector<std::string> splitStatements(std::string_view script);

}  // namespace tinysql
