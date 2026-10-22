/// @file Output.cpp
/// @brief Console text and the HTML page with its testing tools.

#include "Output.hpp"

#include <algorithm>
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
/// @param separator Text between two columns.
/// @return The header line.
std::string headerLine(const ResultSet& table, std::string_view separator)
{
    std::string line;
    for (std::size_t i = 0; i < table.headers.size(); ++i) {
        if (i > 0) {
            line += separator;
        }
        line += fit(table.headers[i], table.widths[i], '-');
    }
    return line;
}

/// @brief Escapes text for use inside HTML.
/// @param text Any text.
/// @return The text with `&`, `<`, `>` and `"` replaced by HTML entities.
std::string escapeHtml(std::string_view text)
{
    std::string result;
    result.reserve(text.size());
    for (char c : text) {
        switch (c) {
        case '&': result += "&amp;"; break;
        case '<': result += "&lt;"; break;
        case '>': result += "&gt;"; break;
        case '"': result += "&quot;"; break;
        default: result += c;
        }
    }
    return result;
}

/// @brief The CSS of the HTML page.
constexpr std::string_view kPageStyle = R"(
body { font-family: system-ui, sans-serif; margin: 2rem; color: #222; }
textarea { width: 100%; max-width: 60rem; font-family: monospace; }
table { border-collapse: collapse; margin: 0.5rem 0 1.5rem; }
th, td { border: 1px solid #ccc; padding: 0.2rem 0.6rem; text-align: left; font-family: monospace; }
th { background: #f2f2f2; }
.error { color: #b00020; }
.tools, form.add-row { display: flex; flex-wrap: wrap; gap: 0.5rem 1rem; align-items: center; margin: 0 0 1rem; }
form.add-row { padding: 0.6rem 0.8rem; background: #f7f7f7; border: 1px solid #ddd; border-radius: 6px; }
form.add-row[hidden] { display: none; }
form.add-row strong { width: 100%; }
.hint { color: #666; font-size: 0.9em; }
button.delete { color: #b00020; }
td.actions { white-space: nowrap; }
tr.editing td { background: #fffbe6; }
tr.editing input { font-family: monospace; }
)";

/// @brief The JavaScript of the HTML page.
///
/// The buttons only write SQL into the box and submit it, so every action is a statement you can read.
constexpr std::string_view kPageScript = R"(<script>
const form = document.getElementById('sql');
const tableSelect = document.getElementById('table');
const run = (sql) => { form.elements.subject.value = sql; form.submit(); };
const quote = (value) => "'" + value.replaceAll("'", "''") + "'";

tableSelect.addEventListener('change', () => {
  for (const f of document.querySelectorAll('form.add-row')) f.hidden = f.dataset.table !== tableSelect.value;
});

document.addEventListener('click', (event) => {
  const button = event.target.closest('button[data-sql], button[data-action]');
  if (!button) return;
  if (button.dataset.confirm && !confirm(button.dataset.confirm)) return;
  const table = tableSelect.value;
  if (button.dataset.action === 'show') run(`SELECT * FROM ${table}`);
  else if (button.dataset.action === 'describe') run(`DESCRIBE ${table}`);
  else run(button.dataset.sql);
});

// Edit: turn the row's editable cells into inputs. Save sends an UPDATE with only the changed columns.
function startEdit(row, table, id) {
  if (row.classList.contains('editing')) return;
  const original = row.innerHTML;
  const cancel = () => { row.innerHTML = original; row.classList.remove('editing'); };
  const save = () => {
    const changes = [...row.querySelectorAll('td[data-col] input')]
      .filter((input) => input.value.trim() !== input.dataset.original)
      .map((input) => `${input.closest('td').dataset.col} = ${quote(input.value.trim())}`);
    if (changes.length === 0) { cancel(); return; }
    run(`UPDATE ${table} SET ${changes.join(', ')} WHERE ID = ${id};SELECT * FROM ${table}`);
  };

  row.classList.add('editing');
  for (const cell of row.querySelectorAll('td[data-col]')) {
    const input = document.createElement('input');
    input.value = input.dataset.original = cell.textContent;
    input.maxLength = Number(cell.dataset.width);
    input.size = Math.min(Number(cell.dataset.width), 16);
    input.addEventListener('keydown', (event) => {
      if (event.key === 'Enter') save();
      if (event.key === 'Escape') cancel();
    });
    cell.replaceChildren(input);
  }
  const actions = row.querySelector('td.actions');
  actions.innerHTML = '<button type="button" class="save">Save</button> <button type="button" class="cancel">Cancel</button>';
  actions.querySelector('.save').addEventListener('click', save);
  actions.querySelector('.cancel').addEventListener('click', cancel);
  row.querySelector('td[data-col] input')?.focus();
}

document.addEventListener('click', (event) => {
  const button = event.target.closest('button.edit');
  if (button) startEdit(button.closest('tr'), button.dataset.table, button.dataset.id);
});

for (const f of document.querySelectorAll('form.add-row')) {
  f.addEventListener('submit', (event) => {
    event.preventDefault();
    const values = [...f.querySelectorAll('input')].map((input) => quote(input.value.trim()));
    run(`INSERT INTO ${f.dataset.table} VALUES (${values.join(',')});SELECT * FROM ${f.dataset.table}`);
  });
}
</script>
)";

/// @brief The query shown in the box on a first visit.
constexpr std::string_view kExampleQuery = "SELECT * FROM EMPLOY WHERE ID <= 5";

/// @brief Chooses the table the page's tools start on.
/// @param results The results being shown.
/// @param tables All tables.
/// @return The table the last statement used; else EMPLOY if it exists; else the first table.
std::string selectedTable(const std::vector<StatementResult>& results, const std::vector<TableSchema>& tables)
{
    for (auto it = results.rbegin(); it != results.rend(); ++it) {
        if (!it->tableName.empty()) {
            return it->tableName;
        }
    }
    for (const TableSchema& table : tables) {
        if (equalsIgnoreCase(table.name, "EMPLOY")) {
            return table.name;
        }
    }
    return tables.empty() ? std::string{} : tables.front().name;
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
        out += std::string(trimRight(headerLine(*table, " "))) + '\n';
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

std::string renderHtml(const std::vector<StatementResult>& results, const std::optional<std::string>& sql,
                       const std::vector<TableSchema>& tables)
{
    // First visit: start with a real query in the box, so clicking Run shows something at once.
    const std::string_view text = sql ? std::string_view(*sql) : kExampleQuery;
    const std::string selected = selectedTable(results, tables);

    std::string page = std::format(
        "<!doctype html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n<title>myTinySQL</title>\n"
        "<style>{}</style>\n</head>\n<body>\n<h1>myTinySQL</h1>\n",
        kPageStyle);

    // Toolbar: pick a table, then show it, describe it, or list all tables.
    page += "<div class=\"tools\">\n<label>Table <select id=\"table\">";
    for (const TableSchema& table : tables) {
        page += std::format("<option{}>{}</option>", table.name == selected ? " selected" : "", escapeHtml(table.name));
    }
    page += "</select></label>\n"
            "<button type=\"button\" data-action=\"show\">Show rows</button>\n"
            "<button type=\"button\" data-action=\"describe\">Describe</button>\n"
            "<button type=\"button\" data-sql=\"DESCRIBE\">List tables</button>\n</div>\n";

    // One "add a row" form per table; the script shows only the selected table's form.
    for (const TableSchema& table : tables) {
        if (equalsIgnoreCase(table.name, kCatalogTable)) {
            continue;  // the catalogue is edited by hand, not from the page
        }
        page += std::format("<form class=\"add-row\" data-table=\"{}\"{}>\n<strong>Add a row to {}</strong>\n",
                            escapeHtml(table.name), table.name == selected ? "" : " hidden", escapeHtml(table.name));
        for (std::size_t i = table.hasAutoId() ? 1 : 0; i < table.columns.size(); ++i) {
            const Column& column = table.columns[i];
            page += std::format("<label>{0} <input maxlength=\"{1}\" size=\"{2}\"></label>\n", escapeHtml(column.name),
                                column.width(), std::min<std::size_t>(column.width(), 16));
        }
        page += std::format("<button type=\"submit\">Add row</button>{}\n</form>\n",
                            table.hasAutoId() ? " <span class=\"hint\">ID is assigned automatically</span>" : "");
    }

    page += std::format("<form id=\"sql\" method=\"get\" action=\"/\">\n"
                        "<textarea name=\"subject\" rows=\"4\">{}</textarea><br>\n"
                        "<button type=\"submit\">Run</button>\n</form>\n",
                        escapeHtml(text));

    const bool nothingToShow = std::ranges::all_of(results, [](const StatementResult& r) { return r.silent; });
    if (sql && nothingToShow) {
        page += "<p class=\"error\">Nothing to show: type a SELECT, INSERT, UPDATE, DELETE or DESCRIBE statement, "
                "then click Run.</p>\n";
    }

    for (const StatementResult& result : results) {
        if (result.silent) {
            continue;
        }
        page += std::format("<h2><code>{}</code></h2>\n", escapeHtml(result.sql));
        if (result.failed) {
            page += std::format("<p class=\"error\">ERROR: {}</p>\n", escapeHtml(result.message));
            continue;
        }
        if (const auto& table = result.table) {
            page += "<table>\n<tr>";
            for (const auto& header : table->headers) {
                page += std::format("<th>{}</th>", escapeHtml(header));
            }
            page += table->idColumn ? "<th></th></tr>\n" : "</tr>\n";
            for (const auto& row : table->rows) {
                page += "<tr>";
                for (std::size_t i = 0; i < row.size(); ++i) {
                    // A cell the Edit button may turn into an input: a real column, not the ID, in a table with IDs.
                    const bool editable = table->idColumn && i != *table->idColumn && !table->literal[i];
                    if (editable) {
                        page += std::format("<td data-col=\"{}\" data-width=\"{}\">{}</td>", escapeHtml(table->headers[i]),
                                            table->storageWidths[i], escapeHtml(row[i]));
                    } else {
                        page += std::format("<td>{}</td>", escapeHtml(row[i]));
                    }
                }
                if (table->idColumn) {
                    // Edit this row in place, or delete it by its ID; then the table is shown again.
                    const std::string& id = row[*table->idColumn];
                    const std::string deleteSql = std::format("DELETE FROM {0} WHERE ID = {1};SELECT * FROM {0}",
                                                              result.tableName, id);
                    page += std::format("<td class=\"actions\">"
                                        "<button type=\"button\" class=\"edit\" data-table=\"{0}\" data-id=\"{1}\">Edit</button> "
                                        "<button type=\"button\" class=\"delete\" data-sql=\"{2}\" "
                                        "data-confirm=\"Delete the row with ID {1} from {0}?\">Delete</button></td>",
                                        escapeHtml(result.tableName), escapeHtml(id), escapeHtml(deleteSql));
                }
                page += "</tr>\n";
            }
            page += "</table>\n";
        }
        page += std::format("<p>{}</p>\n", escapeHtml(result.message));
    }
    return page + std::string(kPageScript) + "</body>\n</html>\n";
}

}  // namespace tinysql
