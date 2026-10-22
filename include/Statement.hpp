#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace tinysql {

/// One entry of a SELECT list: `*`, a column name, or a 'quoted literal' printed on every row.
struct SelectItem {
    enum class Kind { Star, Column, Literal };
    Kind kind;
    std::string text;
};

/// WHERE <column> <op> <value>, with op one of = != <> < <= > >=
struct Condition {
    std::string column;
    std::string op;
    std::string value;
};

struct SelectStmt {
    std::vector<SelectItem> items;
    std::string table;
    std::optional<Condition> where;
};

struct InsertStmt {
    std::string table;
    std::vector<std::string> values;
};

/// DELETE FROM <table> WHERE <condition>. The WHERE is required, so a table is never emptied by accident.
struct DeleteStmt {
    std::string table;
    Condition where;
};

/// One `<column> = <value>` of an UPDATE's SET list.
struct Assignment {
    std::string column;
    std::string value;
};

/// UPDATE <table> SET <col> = <value>[, ...] WHERE <condition>. The WHERE is required, as for DELETE.
struct UpdateStmt {
    std::string table;
    std::vector<Assignment> assignments;
    Condition where;
};

/// COLUMN <name> FORMAT A<width>
struct ColumnFormatStmt {
    std::string column;
    std::size_t width;
};

/// DESCRIBE lists the tables; DESCRIBE <table> lists its columns.
struct DescribeStmt {
    std::optional<std::string> table;
};

/// REM ..., -- ..., or an empty line.
struct CommentStmt {};

using Statement =
    std::variant<SelectStmt, InsertStmt, UpdateStmt, DeleteStmt, ColumnFormatStmt, DescribeStmt, CommentStmt>;

/// Parses one statement. Throws SqlError on a syntax error.
Statement parseStatement(std::string_view sql);

/// Splits a script into statements: one per line, and `;` outside quotes also ends one.
std::vector<std::string> splitStatements(std::string_view script);

}  // namespace tinysql
