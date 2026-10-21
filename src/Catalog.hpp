#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tinysql {

/// The catalogue table: it describes every table, itself included, and is its own data file.
inline constexpr std::string_view kCatalogTable = "SYSTEM_TABLE";

/// One column of a table: the characters it occupies in every fixed-width row.
struct Column {
    std::string name;
    std::string type;      // "N1" or "S1": recorded in SYSTEM_TABLE, not enforced
    std::size_t from = 0;  // first character, counted from 0
    std::size_t to = 0;    // last character, inclusive

    std::size_t width() const { return to - from + 1; }
};

struct TableSchema {
    std::string name;
    std::vector<Column> columns;
    std::size_t recordLength = 0;

    /// Position of the column called `columnName` (case-insensitive), if there is one.
    std::optional<std::size_t> indexOf(std::string_view columnName) const;

    /// True when the first column is ID, which INSERT fills in automatically.
    bool hasAutoId() const;
};

/// The table definitions stored in the SYSTEM_TABLE file: one 80-character line per column.
class Catalog {
public:
    static Catalog load(const std::filesystem::path& dataDir);

    /// Throws SqlError if there is no such table.
    const TableSchema& table(std::string_view name) const;
    const std::vector<TableSchema>& tables() const { return tables_; }

    /// SYSTEM_TABLE is its own data file; every other table T lives in T_DATA.
    std::filesystem::path dataFile(const TableSchema& table) const;

private:
    std::filesystem::path dataDir_;
    std::vector<TableSchema> tables_;
};

}  // namespace tinysql
