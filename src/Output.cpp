/// @file Output.cpp
/// @brief Console text, and the JSON answers of the browser server's API.

#include "Output.hpp"

#include <format>

#include "Util.hpp"

namespace tinysql {

namespace {

/// @brief Cuts or pads text to an exact width.
/// @param text The text.
/// @param width The width wanted.
/// @param filler The character used for padding (space for values, dash for headers).
/// @return Exactly `width` characters.
std::string fit(std::string_view text, std::size_t width, char filler)
{
    std::string result(text.substr(0, width));
    result.resize(width, filler);
    return result;
}

/// @brief Builds the dashed header line of a console table, e.g. `ID-------- NOME----`.
/// @param table The result set.
/// @return The header line, columns separated by one space.
std::string headerLine(const ResultSet& table)
{
    std::string line;
    for (std::size_t i = 0; i < table.headers.size(); ++i) {
        if (i > 0) {
            line += ' ';
        }
        line += fit(table.headers[i], table.widths[i], '-');
    }
    return line;
}

/// @brief Writes text as a JSON string: in double quotes, with quotes, backslashes and control characters escaped.
/// @param text Any text (UTF-8 is passed through unchanged).
/// @return The JSON string; for example, the text `say "hi"` becomes `"say \"hi\""`.
std::string jsonString(std::string_view text)
{
    std::string out = "\"";
    for (const unsigned char c : text) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                out += std::format("\\u{:04x}", c);  // other control characters
            } else {
                out += static_cast<char>(c);
            }
        }
    }
    return out + "\"";
}

}  // namespace

std::string renderText(const StatementResult& result)
{
    if (result.silent) {
        return {};
    }
    std::string out = std::format("> {}\n", result.sql);
    if (result.failed) {
        return out + std::format("ERROR: {}\n\n", result.message);
    }
    if (const auto& table = result.table) {
        out += std::string(trimRight(headerLine(*table))) + '\n';
        for (const auto& row : table->rows) {
            std::string line;
            for (std::size_t i = 0; i < row.size(); ++i) {
                if (i > 0) {
                    line += ' ';
                }
                line += fit(row[i], table->widths[i], ' ');
            }
            out += std::string(trimRight(line)) + '\n';
        }
        out += std::format("({})\n\n", result.message);
    } else {
        out += result.message + "\n\n";
    }
    return out;
}

std::string renderJson(const std::vector<StatementResult>& results)
{
    std::string out = "{\"results\":[";
    bool first = true;
    for (const StatementResult& result : results) {
        if (result.silent) {
            continue;  // COLUMN and comments have nothing to show
        }
        out += first ? "" : ",";
        first = false;
        out += std::format("{{\"sql\":{},\"ok\":{},\"message\":{},\"table\":{}", jsonString(result.sql),
                           result.failed ? "false" : "true", jsonString(result.message), jsonString(result.tableName));

        if (const auto& table = result.table) {
            out += ",\"columns\":[";
            for (std::size_t i = 0; i < table->headers.size(); ++i) {
                // A cell can be edited when the rows have IDs, and it is a real column other than the ID.
                const bool editable = table->idColumn && i != *table->idColumn && !table->literal[i];
                out += std::format("{}{{\"name\":{},\"width\":{},\"editable\":{}}}", i > 0 ? "," : "",
                                   jsonString(table->headers[i]), table->storageWidths[i], editable ? "true" : "false");
            }
            out += "],\"rows\":[";
            for (std::size_t r = 0; r < table->rows.size(); ++r) {
                out += r > 0 ? ",[" : "[";
                for (std::size_t i = 0; i < table->rows[r].size(); ++i) {
                    out += (i > 0 ? "," : "") + jsonString(table->rows[r][i]);
                }
                out += "]";
            }
            out += "],\"idColumn\":" + (table->idColumn ? std::to_string(*table->idColumn) : std::string("null"));
        }
        out += "}";
    }
    return out + "]}";
}

std::string renderTablesJson(const std::vector<TableSchema>& tables)
{
    std::string out = "{\"tables\":[";
    for (std::size_t t = 0; t < tables.size(); ++t) {
        const TableSchema& table = tables[t];
        out += std::format("{}{{\"name\":{},\"readOnly\":{},\"autoId\":{},\"columns\":[", t > 0 ? "," : "",
                           jsonString(table.name), equalsIgnoreCase(table.name, kCatalogTable) ? "true" : "false",
                           table.hasAutoId() ? "true" : "false");
        for (std::size_t i = 0; i < table.columns.size(); ++i) {
            out += std::format("{}{{\"name\":{},\"width\":{}}}", i > 0 ? "," : "",
                               jsonString(table.columns[i].name), table.columns[i].width());
        }
        out += "]}";
    }
    return out + "]}";
}

}  // namespace tinysql
