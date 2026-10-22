/// @file Catalog.hpp
/// @brief The table definitions read from `SYSTEM_TABLE`: tables, their columns and positions.

#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tinysql {

/// @brief Name of the catalogue table: it describes every table, itself included, and is its own data file.
inline constexpr std::string_view kCatalogTable = "SYSTEM_TABLE";

/// @brief One column of a table: the characters it occupies in every fixed-width row.
struct Column {
    std::string name;      ///< Column name, e.g. `NOME`.
    std::string type;      ///< `N1` or `S1`: recorded in SYSTEM_TABLE, not enforced.
    std::size_t from = 0;  ///< First character of the column in a row, counted from 0.
    std::size_t to = 0;    ///< Last character of the column, inclusive.

    /// @brief Number of characters the column occupies.
    /// @return `to - from + 1`.
    std::size_t width() const { return to - from + 1; }
};

/// @brief The definition of one table: its name, its columns, and the length of a row.
struct TableSchema {
    std::string name;              ///< Table name, e.g. `EMPLOY`.
    std::vector<Column> columns;   ///< Columns in file order.
    std::size_t recordLength = 0;  ///< Length of a full row, in characters.

    /// @brief Finds a column by name.
    /// @param columnName The name to look for (case-insensitive).
    /// @return The column's position in #columns, or `std::nullopt` if there is no such column.
    std::optional<std::size_t> indexOf(std::string_view columnName) const;

    /// @brief Tells whether rows are numbered automatically.
    /// @return True when the first column is called ID; INSERT then fills it in.
    bool hasAutoId() const;
};

/// @brief All table definitions, read from the `SYSTEM_TABLE` file (one 80-character line per column).
class Catalog {
public:
    /// @brief Reads `SYSTEM_TABLE` from a data directory.
    /// @param dataDir The directory holding `SYSTEM_TABLE` and the data files.
    /// @return The catalogue with every table it defines.
    /// @throws SqlError If the file cannot be opened or a line is malformed.
    static Catalog load(const std::filesystem::path& dataDir);

    /// @brief Finds a table by name.
    /// @param name The table name (case-insensitive).
    /// @return The table's definition.
    /// @throws SqlError If there is no such table.
    const TableSchema& table(std::string_view name) const;

    /// @brief All tables, in the order they appear in `SYSTEM_TABLE`.
    /// @return The table definitions.
    const std::vector<TableSchema>& tables() const { return tables_; }

    /// @brief The file holding a table's rows.
    /// @param table The table.
    /// @return `SYSTEM_TABLE` for the catalogue itself; `<name>_DATA` for every other table.
    std::filesystem::path dataFile(const TableSchema& table) const;

private:
    std::filesystem::path dataDir_;    ///< Where the files are.
    std::vector<TableSchema> tables_;  ///< Every table, in file order.
};

}  // namespace tinysql
