#include "Catalog.hpp"

#include <algorithm>
#include <format>
#include <fstream>

#include "Util.hpp"

namespace tinysql {

namespace {

// SYSTEM_TABLE describes itself in its first lines; these are the positions of its own fields.
struct Field {
    std::size_t from;
    std::size_t to;
};
constexpr Field kTable{15, 34};
constexpr Field kName{35, 54};
constexpr Field kType{55, 59};
constexpr Field kFrom{60, 64};
constexpr Field kTo{65, 69};
constexpr Field kRecordLength{75, 79};

std::string_view cut(std::string_view line, Field field)
{
    if (field.from >= line.size()) {
        return {};
    }
    return trim(line.substr(field.from, field.to - field.from + 1));
}

std::size_t number(std::string_view line, Field field, std::size_t lineNumber)
{
    auto value = parseInteger(cut(line, field));
    if (!value || *value < 0) {
        throw SqlError(std::format("{} line {}: expected a number in characters {}-{}",
                                   kCatalogTable, lineNumber, field.from, field.to));
    }
    return static_cast<std::size_t>(*value);
}

}  // namespace

std::optional<std::size_t> TableSchema::indexOf(std::string_view columnName) const
{
    auto it = std::ranges::find_if(columns, [&](const Column& c) { return equalsIgnoreCase(c.name, columnName); });
    if (it == columns.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(it - columns.begin());
}

bool TableSchema::hasAutoId() const
{
    return !columns.empty() && equalsIgnoreCase(columns.front().name, "ID");
}

Catalog Catalog::load(const std::filesystem::path& dataDir)
{
    Catalog catalog;
    catalog.dataDir_ = dataDir;

    const auto path = dataDir / kCatalogTable;
    std::ifstream in(path);
    if (!in) {
        throw SqlError(std::format("cannot open the catalogue {}", path.string()));
    }

    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(in, line)) {
        ++lineNumber;
        if (trim(line).empty()) {
            continue;
        }
        std::string tableName(cut(line, kTable));
        Column column{
            .name = std::string(cut(line, kName)),
            .type = std::string(cut(line, kType)),
            .from = number(line, kFrom, lineNumber),
            .to = number(line, kTo, lineNumber),
        };
        if (tableName.empty() || column.name.empty() || column.to < column.from) {
            throw SqlError(std::format("{} line {}: invalid column definition", kCatalogTable, lineNumber));
        }
        const std::size_t recordLength = std::max(number(line, kRecordLength, lineNumber), column.to + 1);

        auto table = std::ranges::find_if(catalog.tables_,
                                          [&](const TableSchema& t) { return equalsIgnoreCase(t.name, tableName); });
        if (table == catalog.tables_.end()) {
            catalog.tables_.push_back(TableSchema{.name = tableName, .columns = {}, .recordLength = 0});
            table = std::prev(catalog.tables_.end());
        }
        table->columns.push_back(std::move(column));
        table->recordLength = std::max(table->recordLength, recordLength);
    }
    return catalog;
}

const TableSchema& Catalog::table(std::string_view name) const
{
    auto it = std::ranges::find_if(tables_, [&](const TableSchema& t) { return equalsIgnoreCase(t.name, name); });
    if (it == tables_.end()) {
        throw SqlError(std::format("unknown table '{}'", name));
    }
    return *it;
}

std::filesystem::path Catalog::dataFile(const TableSchema& table) const
{
    if (equalsIgnoreCase(table.name, kCatalogTable)) {
        return dataDir_ / kCatalogTable;
    }
    return dataDir_ / (table.name + "_DATA");
}

}  // namespace tinysql
