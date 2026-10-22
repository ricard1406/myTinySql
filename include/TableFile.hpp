/// @file TableFile.hpp
/// @brief Reading, appending, editing and removing the rows of one table's data file.

#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Catalog.hpp"

namespace tinysql {

/// @brief One row of a table: one value per column, trailing spaces removed.
using Row = std::vector<std::string>;

/// @brief Reads and changes the rows of one table's data file.
///
/// Each line of the file is one row; each column sits at the fixed positions given by the schema.
/// Changes that rewrite the file (removeRows(), updateRows()) go through a temporary file,
/// so a crash half-way never leaves a half-written table.
class TableFile {
public:
    /// @brief Prepares access to one data file.
    /// @param schema The table's definition; it must outlive this object.
    /// @param path The data file, usually from Catalog::dataFile().
    TableFile(const TableSchema& schema, std::filesystem::path path);

    /// @brief Reads every row of the file.
    /// @return The rows, each split into one value per column; blank lines are skipped.
    /// @throws SqlError If the file does not exist.
    std::vector<Row> readRows() const;

    /// @brief The ID to give the next new row.
    /// @return The highest numeric ID in the file plus one, or 1 for an empty or missing file.
    long long nextId() const;

    /// @brief Adds one row at the end of the file.
    /// @param values One value per column, ID included.
    /// @throws SqlError If the number of values is wrong, a value is too long for its column,
    ///         or the file cannot be written.
    void append(const Row& values) const;

    /// @brief Removes the rows that match a test.
    /// @param shouldRemove Called for every row; returns true to remove it.
    /// @return How many rows were removed.
    /// @throws SqlError If the file does not exist or cannot be rewritten.
    std::size_t removeRows(const std::function<bool(const Row&)>& shouldRemove) const;

    /// @brief Changes some columns in the rows that match a test.
    /// @param shouldUpdate Called for every row; returns true to change it.
    /// @param newValues Pairs of (column index, new value) to set in every matching row.
    /// @return How many rows were changed.
    /// @throws SqlError If a new value is too long for its column (checked before the file is touched),
    ///         or the file does not exist or cannot be rewritten.
    std::size_t updateRows(const std::function<bool(const Row&)>& shouldUpdate,
                           const std::vector<std::pair<std::size_t, std::string>>& newValues) const;

private:
    /// @brief What rewrite() does with one line.
    enum class Change {
        Keep,    ///< Write the line back unchanged.
        Remove,  ///< Leave the line out.
        Update,  ///< Write the line back as `decide` edited it.
    };

    /// @brief Rewrites the file through a temporary copy, line by line.
    /// @param decide Called for every line with the raw line (which it may edit in place)
    ///        and the same line split into a Row; returns what to do with that line.
    /// @return How many lines were removed or edited; when 0, the file is left untouched.
    /// @throws SqlError If the file does not exist or the temporary file cannot be written.
    std::size_t rewrite(const std::function<Change(std::string& line, const Row& row)>& decide) const;

    /// @brief Cuts one line of the file into its column values.
    /// @param line A raw line; a line shorter than the row length gives empty values at the end.
    /// @return One value per column, trailing spaces removed.
    Row splitLine(std::string_view line) const;

    const TableSchema& schema_;  ///< The table's definition.
    std::filesystem::path path_;  ///< The data file.
};

}  // namespace tinysql
