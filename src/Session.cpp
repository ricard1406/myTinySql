/// @file Session.cpp
/// @brief Implementation of Session: running each kind of statement.

#include "Session.hpp"

#include <algorithm>
#include <compare>
#include <format>
#include <fstream>

#include "Catalog.hpp"
#include "TableFile.hpp"
#include "Util.hpp"

namespace tinysql {

namespace {

/// @brief Applies a WHERE test to one value.
/// @param fieldValue The row's value in the tested column.
/// @param condition The test.
/// @return True if the value passes. `LIKE` matches a pattern, ignoring case (see matchesLike()).
///         The other operators compare numbers as numbers when both sides are integers,
///         otherwise as case-sensitive text.
bool matches(std::string_view fieldValue, const Condition& condition)
{
    if (condition.op == "LIKE") {
        return matchesLike(fieldValue, condition.value);
    }

    std::strong_ordering order = std::strong_ordering::equal;
    auto left = parseInteger(fieldValue);
    auto right = parseInteger(condition.value);
    if (left && right) {
        order = *left <=> *right;
    } else {
        order = trim(fieldValue) <=> trim(condition.value);
    }

    const std::string& op = condition.op;
    if (op == "=") return order == 0;
    if (op == "!=" || op == "<>") return order != 0;
    if (op == "<") return order < 0;
    if (op == "<=") return order <= 0;
    if (op == ">") return order > 0;
    return order >= 0;  // ">="
}

/// @brief Builds a result whose column widths fit the headers and the values (used by DESCRIBE).
/// @param headers The column titles.
/// @param rows The values, one inner vector per row.
/// @return The result set.
ResultSet fitted(std::vector<std::string> headers, std::vector<std::vector<std::string>> rows)
{
    ResultSet result;
    result.widths.resize(headers.size());
    for (std::size_t i = 0; i < headers.size(); ++i) {
        result.widths[i] = headers[i].size();
        for (const auto& row : rows) {
            result.widths[i] = std::max(result.widths[i], row[i].size());
        }
    }
    result.literal.assign(headers.size(), false);
    result.storageWidths.assign(headers.size(), 0);
    result.headers = std::move(headers);
    result.rows = std::move(rows);
    return result;
}

/// @brief Writes a count with a noun in singular or plural.
/// @param n The count.
/// @param word The noun in singular, e.g. "row".
/// @return E.g. "1 row" or "3 rows".
std::string plural(std::size_t n, std::string_view word)
{
    return std::format("{} {}{}", n, word, n == 1 ? "" : "s");
}

/// @brief Longest table or column name: the width of the TABLE and FIELD columns of SYSTEM_TABLE.
constexpr std::size_t kMaxNameLength = 20;

/// @brief Longest row: SYSTEM_TABLE keeps column positions in 5 characters.
constexpr std::size_t kMaxRecordLength = 99999;

/// @brief Checks a new table or column name.
/// @param name The name.
/// @param what "table" or "column", for the error message.
/// @throws SqlError Unless the name starts with a letter, has only letters, digits, `_` and `-`,
///         and is at most kMaxNameLength characters long.
void checkName(std::string_view name, std::string_view what)
{
    auto isLetter = [](char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); };
    auto isAllowed = [&](char c) { return isLetter(c) || (c >= '0' && c <= '9') || c == '_' || c == '-'; };
    if (name.empty() || name.size() > kMaxNameLength || !isLetter(name.front())
        || !std::ranges::all_of(name, isAllowed)) {
        throw SqlError(std::format("invalid {} name '{}': use up to {} letters, digits, _ and -, starting with a letter",
                                   what, name, kMaxNameLength));
    }
}

}  // namespace

Session::Session(std::filesystem::path dataDir) : dataDir_(std::move(dataDir)) {}

StatementResult Session::execute(std::string_view sql)
{
    StatementResult result;
    try {
        const Statement statement = parseStatement(sql);
        result = std::visit([this](const auto& stmt) { return run(stmt); }, statement);
    } catch (const SqlError& e) {
        result.failed = true;
        result.message = e.what();
    } catch (const std::exception& e) {
        result.failed = true;
        result.message = std::format("internal error: {}", e.what());
    }
    result.sql = std::string(trim(sql));
    return result;
}

std::vector<StatementResult> Session::executeScript(std::string_view script)
{
    std::vector<StatementResult> results;
    for (const std::string& sql : splitStatements(script)) {
        results.push_back(execute(sql));
    }
    return results;
}

StatementResult Session::run(const SelectStmt& stmt)
{
    const Catalog catalog = Catalog::load(dataDir_);
    const TableSchema& schema = catalog.table(stmt.table);

    // What each output column shows: a table column (by index) or a literal text.
    struct Output {
        std::string header;
        std::optional<std::size_t> source;
        std::size_t width;
    };
    std::vector<Output> outputs;
    auto addColumn = [&](std::size_t index) {
        const Column& column = schema.columns[index];
        auto custom = displayWidths_.find(toUpper(column.name));
        // COLUMN ... FORMAT wins; otherwise the column is wide enough for its values and its name.
        const std::size_t width = custom != displayWidths_.end() ? custom->second
                                                                 : std::max(column.width(), column.name.size());
        outputs.push_back({column.name, index, width});
    };
    for (const SelectItem& item : stmt.items) {
        switch (item.kind) {
        case SelectItem::Kind::Star:
            for (std::size_t i = 0; i < schema.columns.size(); ++i) {
                addColumn(i);
            }
            break;
        case SelectItem::Kind::Column: {
            auto index = schema.indexOf(item.text);
            if (!index) {
                throw SqlError(std::format("unknown column '{}' in table {}", item.text, schema.name));
            }
            addColumn(*index);
            break;
        }
        case SelectItem::Kind::Literal:
            outputs.push_back({item.text, std::nullopt, item.text.size()});
            break;
        }
    }

    ResultSet result;

    std::optional<std::size_t> whereIndex;
    if (stmt.where) {
        whereIndex = schema.indexOf(stmt.where->column);
        if (!whereIndex) {
            throw SqlError(std::format("unknown column '{}' in table {}", stmt.where->column, schema.name));
        }
    }

    for (const Output& output : outputs) {
        result.headers.push_back(output.header);
        result.widths.push_back(output.width);
        result.literal.push_back(!output.source);
        result.storageWidths.push_back(output.source ? schema.columns[*output.source].width() : 0);
    }

    // Rows can be edited and deleted by ID when the table has an ID column, the result shows it,
    // and the table is not the catalogue itself.
    if (schema.hasAutoId() && !equalsIgnoreCase(schema.name, kCatalogTable)) {
        auto id = std::ranges::find_if(outputs, [](const Output& o) { return o.source == std::size_t{0}; });
        if (id != outputs.end()) {
            result.idColumn = static_cast<std::size_t>(id - outputs.begin());
        }
    }

    for (const Row& row : TableFile(schema, catalog.dataFile(schema)).readRows()) {
        if (whereIndex && !matches(row[*whereIndex], *stmt.where)) {
            continue;
        }
        std::vector<std::string> values;
        values.reserve(outputs.size());
        for (const Output& output : outputs) {
            values.push_back(output.source ? row[*output.source] : output.header);
        }
        result.rows.push_back(std::move(values));
    }

    StatementResult statementResult;
    statementResult.tableName = schema.name;
    statementResult.message = plural(result.rows.size(), "row");
    statementResult.table = std::move(result);
    return statementResult;
}

StatementResult Session::run(const InsertStmt& stmt)
{
    const Catalog catalog = Catalog::load(dataDir_);
    const TableSchema& schema = catalog.table(stmt.table);
    const TableFile file(schema, catalog.dataFile(schema));

    Row values = stmt.values;
    const std::size_t columns = schema.columns.size();
    if (schema.hasAutoId() && values.size() + 1 == columns) {
        values.insert(values.begin(), std::to_string(file.nextId()));
    } else if (values.size() != columns) {
        throw SqlError(std::format("table {} has {} columns{}: got {} values", schema.name, columns,
                                   schema.hasAutoId() ? std::format(" (give {} and ID is assigned)", columns - 1) : "",
                                   values.size()));
    } else if (schema.hasAutoId()) {
        const auto rows = std::filesystem::exists(catalog.dataFile(schema)) ? file.readRows() : std::vector<Row>{};
        if (std::ranges::any_of(rows, [&](const Row& row) { return row.front() == values.front(); })) {
            throw SqlError(std::format("table {} already has a row with ID {}", schema.name, values.front()));
        }
    }
    file.append(values);

    StatementResult result;
    result.tableName = schema.name;
    result.message = std::format("1 row inserted into {}", schema.name);
    if (schema.hasAutoId()) {
        result.message += std::format(" with ID {}", values.front());
    }
    return result;
}

StatementResult Session::run(const UpdateStmt& stmt)
{
    const Catalog catalog = Catalog::load(dataDir_);
    const TableSchema& schema = catalog.table(stmt.table);
    if (equalsIgnoreCase(schema.name, kCatalogTable)) {
        throw SqlError("SYSTEM_TABLE cannot be changed with UPDATE: edit the file instead");
    }

    std::vector<std::pair<std::size_t, std::string>> newValues;
    for (const Assignment& assignment : stmt.assignments) {
        const auto index = schema.indexOf(assignment.column);
        if (!index) {
            throw SqlError(std::format("unknown column '{}' in table {}", assignment.column, schema.name));
        }
        if (*index == 0 && schema.hasAutoId()) {
            throw SqlError("the ID column cannot be changed: rows are found by their ID");
        }
        newValues.emplace_back(*index, assignment.value);
    }
    const auto whereIndex = schema.indexOf(stmt.where.column);
    if (!whereIndex) {
        throw SqlError(std::format("unknown column '{}' in table {}", stmt.where.column, schema.name));
    }

    const std::size_t updated = TableFile(schema, catalog.dataFile(schema)).updateRows(
        [&](const Row& row) { return matches(row[*whereIndex], stmt.where); }, newValues);

    StatementResult result;
    result.tableName = schema.name;
    result.message = std::format("{} updated in {}", plural(updated, "row"), schema.name);
    return result;
}

StatementResult Session::run(const DeleteStmt& stmt)
{
    const Catalog catalog = Catalog::load(dataDir_);
    const TableSchema& schema = catalog.table(stmt.table);
    if (equalsIgnoreCase(schema.name, kCatalogTable)) {
        throw SqlError("SYSTEM_TABLE cannot be changed with DELETE: edit the file instead");
    }
    const auto whereIndex = schema.indexOf(stmt.where.column);
    if (!whereIndex) {
        throw SqlError(std::format("unknown column '{}' in table {}", stmt.where.column, schema.name));
    }

    const std::size_t removed = TableFile(schema, catalog.dataFile(schema)).removeRows([&](const Row& row) {
        return matches(row[*whereIndex], stmt.where);
    });

    StatementResult result;
    result.tableName = schema.name;
    result.message = std::format("{} deleted from {}", plural(removed, "row"), schema.name);
    return result;
}

StatementResult Session::run(const CreateTableStmt& stmt)
{
    const Catalog catalog = Catalog::load(dataDir_);

    // The new table's definition; names are kept in upper case, like the existing tables.
    checkName(stmt.table, "table");
    TableSchema schema{.name = toUpper(stmt.table), .columns = {}, .recordLength = 0};
    auto sameName = [&](const TableSchema& t) { return equalsIgnoreCase(t.name, schema.name); };
    if (std::ranges::any_of(catalog.tables(), sameName)) {
        throw SqlError(std::format("table {} already exists", schema.name));
    }
    for (const ColumnDefinition& definition : stmt.columns) {
        checkName(definition.name, "column");
        const std::string name = toUpper(definition.name);
        if (schema.indexOf(name)) {
            throw SqlError(std::format("column {} appears twice", name));
        }
        if (name == "ID" && !schema.columns.empty()) {
            throw SqlError("the ID column must be the first one: rows are numbered by it");
        }
        schema.columns.push_back(Column{
            .name = name,
            .type = definition.type,
            .from = schema.recordLength,
            .to = schema.recordLength + definition.width - 1,
        });
        schema.recordLength += definition.width;
    }
    if (schema.recordLength > kMaxRecordLength) {
        throw SqlError(std::format("a row of table {} would be {} characters long: the limit is {}",
                                   schema.name, schema.recordLength, kMaxRecordLength));
    }

    // One SYSTEM_TABLE line per column, numbered after the last line.
    const TableSchema& catalogSchema = catalog.table(kCatalogTable);
    const TableFile catalogFile(catalogSchema, catalog.dataFile(catalogSchema));
    long long id = catalogFile.nextId();
    std::vector<Row> lines;
    for (std::size_t i = 0; i < schema.columns.size(); ++i) {
        const Column& column = schema.columns[i];
        lines.push_back({std::to_string(id++), std::to_string(i + 1), schema.name, column.name, column.type,
                         std::to_string(column.from), std::to_string(column.to), std::to_string(column.width()),
                         std::to_string(schema.recordLength)});
    }

    // First the empty data file, then the catalogue: if the catalogue cannot be written,
    // the data file is removed again and nothing has changed.
    const auto dataFile = catalog.dataFile(schema);
    if (std::filesystem::exists(dataFile)) {
        throw SqlError(std::format("data file {} already exists: remove it, or choose another name",
                                   dataFile.filename().string()));
    }
    if (!std::ofstream(dataFile)) {
        throw SqlError(std::format("cannot create {}", dataFile.string()));
    }
    try {
        catalogFile.appendRows(lines);
    } catch (...) {
        std::filesystem::remove(dataFile);
        throw;
    }

    StatementResult result;
    result.tableName = schema.name;
    result.message = std::format("table {} created with {} (row length {})", schema.name,
                                 plural(schema.columns.size(), "column"), schema.recordLength);
    return result;
}

StatementResult Session::run(const ColumnFormatStmt& stmt)
{
    displayWidths_[toUpper(stmt.column)] = stmt.width;
    StatementResult result;
    result.silent = true;
    return result;
}

StatementResult Session::run(const DescribeStmt& stmt)
{
    const Catalog catalog = Catalog::load(dataDir_);
    std::vector<std::vector<std::string>> rows;
    StatementResult result;

    if (!stmt.table) {
        for (const TableSchema& table : catalog.tables()) {
            rows.push_back({table.name, std::to_string(table.columns.size()), std::to_string(table.recordLength)});
        }
        result.message = plural(rows.size(), "table");
        result.table = fitted({"TABLE", "COLUMNS", "ROW_LENGTH"}, std::move(rows));
        return result;
    }

    const TableSchema& schema = catalog.table(*stmt.table);
    for (const Column& column : schema.columns) {
        rows.push_back({column.name, column.type, std::to_string(column.from), std::to_string(column.to),
                        std::to_string(column.width())});
    }
    result.tableName = schema.name;
    result.message = plural(rows.size(), "column");
    result.table = fitted({"COLUMN", "TYPE", "FROM", "TO", "WIDTH"}, std::move(rows));
    return result;
}

StatementResult Session::run(const CommentStmt&)
{
    StatementResult result;
    result.silent = true;
    return result;
}

}  // namespace tinysql
