/// @file TableFile.cpp
/// @brief Implementation of TableFile: reading rows and writing them back safely.

#include "TableFile.hpp"

#include <algorithm>
#include <format>
#include <fstream>

#include "Util.hpp"

namespace tinysql {

namespace {

/// @brief Checks that a value fits in its column.
/// @param column The column.
/// @param value The value to store.
/// @throws SqlError If the value is longer than the column width.
void checkFits(const Column& column, const std::string& value)
{
    if (value.size() > column.width()) {
        throw SqlError(std::format("value '{}' is too long for column {} (max {} characters)",
                                   value, column.name, column.width()));
    }
}

/// @brief Writes a value into its column's characters of a row, padded with spaces to the column width.
/// @param record The row being built or edited; it must be at least as long as the row length.
/// @param column Where the value goes.
/// @param value The value.
/// @throws SqlError If the value is longer than the column width.
void place(std::string& record, const Column& column, const std::string& value)
{
    checkFits(column, value);
    std::string padded = value;
    padded.resize(column.width(), ' ');
    record.replace(column.from, column.width(), padded);
}

}  // namespace

TableFile::TableFile(const TableSchema& schema, std::filesystem::path path)
    : schema_(schema), path_(std::move(path))
{
}

std::vector<Row> TableFile::readRows() const
{
    std::ifstream in(path_);
    if (!in) {
        throw SqlError(std::format("data file {} not found", path_.filename().string()));
    }

    std::vector<Row> rows;
    std::string line;
    while (std::getline(in, line)) {
        if (trim(line).empty()) {
            continue;
        }
        rows.push_back(splitLine(line));
    }
    return rows;
}

Row TableFile::splitLine(std::string_view line) const
{
    Row row;
    row.reserve(schema_.columns.size());
    for (const Column& column : schema_.columns) {
        std::string_view text = column.from < line.size() ? line.substr(column.from, column.width()) : std::string_view{};
        row.emplace_back(trimRight(text));
    }
    return row;
}

std::size_t TableFile::removeRows(const std::function<bool(const Row&)>& shouldRemove) const
{
    return rewrite([&](std::string&, const Row& row) { return shouldRemove(row) ? Change::Remove : Change::Keep; });
}

std::size_t TableFile::updateRows(const std::function<bool(const Row&)>& shouldUpdate,
                                  const std::vector<std::pair<std::size_t, std::string>>& newValues) const
{
    // Check every value before touching the file, so a bad value changes nothing.
    for (const auto& [index, value] : newValues) {
        checkFits(schema_.columns.at(index), value);
    }
    return rewrite([&](std::string& line, const Row& row) {
        if (!shouldUpdate(row)) {
            return Change::Keep;
        }
        if (line.size() < schema_.recordLength) {
            line.resize(schema_.recordLength, ' ');
        }
        for (const auto& [index, value] : newValues) {
            place(line, schema_.columns[index], value);
        }
        return Change::Update;
    });
}

std::size_t TableFile::rewrite(const std::function<Change(std::string& line, const Row& row)>& decide) const
{
    std::ifstream in(path_);
    if (!in) {
        throw SqlError(std::format("data file {} not found", path_.filename().string()));
    }

    std::vector<std::string> kept;
    std::size_t changed = 0;
    std::string line;
    while (std::getline(in, line)) {
        if (trim(line).empty()) {
            continue;
        }
        const Change change = decide(line, splitLine(line));
        if (change != Change::Keep) {
            ++changed;
        }
        if (change != Change::Remove) {
            kept.push_back(std::move(line));
        }
    }
    in.close();

    if (changed == 0) {
        return 0;  // nothing to rewrite
    }

    std::filesystem::path temporary = path_;
    temporary += ".tmp";
    {
        std::ofstream out(temporary, std::ios::trunc);
        if (!out) {
            throw SqlError(std::format("cannot write {}", temporary.string()));
        }
        for (const std::string& keptLine : kept) {
            out << keptLine << '\n';
        }
        if (!out.flush()) {
            throw SqlError(std::format("cannot write {}", temporary.string()));
        }
    }  // the file is closed here, before it replaces the original
    std::filesystem::rename(temporary, path_);
    return changed;
}

long long TableFile::nextId() const
{
    long long maxId = 0;
    if (std::filesystem::exists(path_)) {
        for (const Row& row : readRows()) {
            if (auto id = parseInteger(row.front())) {
                maxId = std::max(maxId, *id);
            }
        }
    }
    return maxId + 1;
}

void TableFile::append(const Row& values) const
{
    appendRows({values});
}

void TableFile::appendRows(const std::vector<Row>& rows) const
{
    // Build every record first, so a bad value stops before the file is touched.
    std::vector<std::string> records;
    for (const Row& values : rows) {
        if (values.size() != schema_.columns.size()) {
            throw SqlError(std::format("table {} has {} columns, got {} values",
                                       schema_.name, schema_.columns.size(), values.size()));
        }
        std::string record(schema_.recordLength, ' ');
        for (std::size_t i = 0; i < values.size(); ++i) {
            place(record, schema_.columns[i], values[i]);
        }
        records.push_back(std::move(record));
    }

    // Make sure the new row starts on its own line, even if the file lacks a final newline.
    bool needsNewline = false;
    if (std::ifstream existing(path_, std::ios::binary | std::ios::ate); existing && existing.tellg() > 0) {
        existing.seekg(-1, std::ios::end);
        needsNewline = existing.get() != '\n';
    }

    std::ofstream out(path_, std::ios::app);
    if (!out) {
        throw SqlError(std::format("cannot write {}", path_.string()));
    }
    if (needsNewline) {
        out << '\n';
    }
    for (const std::string& record : records) {
        out << record << '\n';
    }
}

}  // namespace tinysql
