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

using Row = std::vector<std::string>;

/// Reads and appends the rows of one table's data file.
/// Each line is one row; each column sits at the fixed positions given by the schema.
class TableFile {
public:
    TableFile(const TableSchema& schema, std::filesystem::path path);

    /// Every row, split into one value per column, trailing spaces removed.
    std::vector<Row> readRows() const;

    /// Highest numeric ID in the file plus one (1 for an empty table).
    long long nextId() const;

    /// Appends one row; `values` holds one value per column, ID included.
    void append(const Row& values) const;

    /// Removes every row for which `shouldRemove` returns true; returns how many were removed.
    /// The kept lines are written to a temporary file that then replaces the original,
    /// so a crash half-way never leaves a half-written table.
    std::size_t removeRows(const std::function<bool(const Row&)>& shouldRemove) const;

    /// In every row for which `shouldUpdate` returns true, sets each (column index, value) pair
    /// of `newValues`; returns how many rows changed. Rewrites the file the same safe way.
    std::size_t updateRows(const std::function<bool(const Row&)>& shouldUpdate,
                           const std::vector<std::pair<std::size_t, std::string>>& newValues) const;

private:
    /// What rewrite() does with one line.
    enum class Change { Keep, Remove, Update };

    /// Reads every line, lets `decide` keep, remove or edit it (in place), and writes the result
    /// to a temporary file that replaces the original. Returns how many lines were removed or edited.
    std::size_t rewrite(const std::function<Change(std::string& line, const Row& row)>& decide) const;

    Row splitLine(std::string_view line) const;

    const TableSchema& schema_;
    std::filesystem::path path_;
};

}  // namespace tinysql
