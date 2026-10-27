# myTinySQL — Project Guide

Copy of the online project guide, as of 2026-10-21, plus the DELETE and UPDATE statements and the web page tools added that day, and the CREATE TABLE statement added on 2026-10-27.

## Overview

myTinySQL is a small SQL interpreter that stores tables as plain-text files with fixed-width columns. It runs SQL scripts, takes statements typed at a prompt, and answers queries from a browser page. It began in August 2023 as a learning project and was rewritten in modern C++ on 2026-10-20.

The 2023 version was **C++** (g++, `std::string`, `std::vector`, `std::thread`), written in a mostly C style: global buffers allocated with `calloc`, manual offset arithmetic and `char[]` copies.

The current version is **C++20**. It has small classes with one job each, `std::vector`, `std::optional` and `std::variant` instead of `calloc` buffers, exceptions instead of `exit()`, and no global variables.

What it supports:

- `SELECT` of columns, `*` or `'quoted text'`, with an optional `WHERE <col> <op> <value>` (`=` `!=` `<>` `<` `<=` `>` `>=`, or `LIKE` to search text)
- `INSERT INTO <table> VALUES (...)`: assigns the next ID and checks every value against its column width
- `UPDATE <table> SET <col> = <value>, ... WHERE <col> <op> <value>`: changes the matching rows (the WHERE is required)
- `DELETE FROM <table> WHERE <col> <op> <value>`: removes the matching rows (the WHERE is required)
- `CREATE TABLE <table> (<col> CHAR(<n>), <col> NUMBER(<n>), ...)`: adds a new, empty table
- `COLUMN <col> FORMAT A<n>` to set a column's display width (the idea comes from Oracle SQL*Plus)
- `DESCRIBE` and `DESCRIBE <table>`, and `REM` and `--` comments
- Three ways to run it: a script file, an interactive prompt, or a browser page on port 8080 (with buttons to show, search, add, edit and delete rows, and to create tables)

This guide describes the rewritten version. It was checked on 2026-10-20 by building with g++ 15.2 (no warnings) and running `make test` (7 test scripts, all passing).

**On GitHub:** the code and its history are at [github.com/ricard1406/myTinySql](https://github.com/ricard1406/myTinySql) (public since 2026-10-22). The first pre-release is [v0.1.0](https://github.com/ricard1406/myTinySql/releases/tag/v0.1.0), and the Doxygen documentation of the code is at [ricard1406.github.io/myTinySql](https://ricard1406.github.io/myTinySql/), rebuilt automatically after every push. Two guides sit next to the code: [INSTALLATION.md](../INSTALLATION.md) (requirements, build, first run) and [TESTING.md](../TESTING.md) (twelve checks, each with its command and expected result).

> **DISCLAIMER: All data in this project is sample data, for testing purposes only. The companies, addresses and other values are fictional or AI generated. Many names in the `EMPLOY` table are those of historical Italian composers, used only as sample values; the other values in those rows (such as city and period) are sample data and may not be accurate. Any other resemblance to real persons, living or dead, or actual places is purely coincidental.**

## Project structure

The code is in `src/` (8 `.cpp` files) and `include/` (7 `.hpp` headers), 2,597 lines in all; the database in `data/`, scripts in `examples/` and `tests/`, and notes in `docs/`.

**Source code (`.cpp` in `src/`, `.hpp` in `include/`)**

| File | Lines | Role |
| --- | --- | --- |
| `main.cpp` | 209 | Reads the command-line options and starts script, interactive or server mode |
| `Statement.hpp`, `Parser.cpp` | 111 + 506 | The statement types and the tokenizer and parser that build them. `splitStatements()` cuts a script into statements. |
| `Session.hpp` / `.cpp` | 111 + 413 | Runs one statement at a time, keeps the `COLUMN` widths, and returns a `StatementResult` |
| `Catalog.hpp` / `.cpp` | 75 + 133 | Reads `SYSTEM_TABLE` into `TableSchema` objects: each table's columns and their positions |
| `TableFile.hpp` / `.cpp` | 93 + 202 | Reads the rows of a `<TABLE>_DATA` file, appends new ones, and edits or removes rows (through a temporary file) |
| `Output.hpp` / `.cpp` | 38 + 148 | Turns results into console text, or into JSON for the browser page |
| `Server.hpp` / `.cpp` | 30 + 370 | A small HTTP server: the page's files from `web/`, and a JSON API (`/api/tables`, `/api/sql`); one thread per connection, SQL one request at a time |
| `Util.hpp` / `.cpp` | 58 + 100 | `SqlError` and string helpers: trim, case-insensitive compare, integer parsing |

**Everything else**

| Path | Content |
| --- | --- |
| `data/` | `SYSTEM_TABLE` (30 lines, 5 tables), `EMPLOY_DATA` (37 rows: ID, NOME, COGNOME, VIA, CITTA, PERIODO), `COMPANY_DATA` (8 made-up companies, added 2026-10-21), `CAL_WEEKLY_DATA` (154 rows), `VINI-ITALY_DATA` (37 Italian wines: REGIONE, VINO, TIPO; made with **+ New table** on 2026-10-27) |
| `web/` | The browser page: `index.html` (structure), `style.css` (look), `app.js` (behaviour), `favicon.svg` (icon) |
| `examples/` | `select.sql`, `describe.sql`, `insert.sql`, `company.sql`. `insert.sql` adds rows to `data/`; `company.sql` holds the rows that filled `COMPANY_DATA`. |
| `tests/` | 7 test scripts, each with its `.expected` output, `run_tests.sh`, and `data/`: the frozen tables the tests run on |
| `docs/` | These notes |
| `Makefile`, `Doxyfile`, `README.md` | Build rules, Doxygen settings and a quick start |
| `INSTALLATION.md`, `TESTING.md` | The installation guide, and the testing guide: twelve checks with their expected results |
| `.github/workflows/doxygen.yml` | Rebuilds the Doxygen pages on GitHub after every push |

One data change from 2023: the ruler line `0123456789...` at the top of `CAL_WEEKLY_DATA` was removed, because it was printed as a row. `SYSTEM_DATA` is no longer needed, since `DESCRIBE` now reads `SYSTEM_TABLE`.

## How it works

Every input, from a script file, the prompt or an HTTP request, becomes SQL text. `splitStatements()` cuts that text at newlines and `;`. `parseStatement()` turns each piece into a typed `Statement`, and `Session::execute()` runs it.

```
     --sqlfile=FILE             no option                --http :8080
     (a script)                 (tinysql> prompt)        (browser page with buttons)
            \                          |                          /
             v                         v                         v
 +-------------------------------------------------------------------------+
 |  main.cpp and Server.cpp: read the script or decode the HTTP request    |
 +-------------------------------------------------------------------------+
                                    |
                                    v
 +-------------------------------------------------------------------------+
 |  Parser.cpp, then Session::execute()                                    |
 |  text -> Statement (std::variant); std::visit picks the matching run()  |
 +-------------------------------------------------------------------------+
        |                  |                   |                  |
        v                  v                   v                  v
   Change rows          SELECT              COLUMN            DESCRIBE
  INSERT, UPDATE,   columns, WHERE      saves a display     tables or
  DELETE, CREATE                        width               columns
        |                  |  \
     writes             reads   \ StatementResult
        v                  v    v
 +--------------------------------+   +--------------------------------------+
 | Catalog.cpp + TableFile.cpp    |   | Output.cpp                           |
 | data/SYSTEM_TABLE: positions   |   | console text, or JSON for the        |
 | data/TABLE_DATA: one row/line  |   | browser page (web/)                  |
 +--------------------------------+   +--------------------------------------+
```
`Statement` is a `std::variant` of `SelectStmt`, `InsertStmt`, `UpdateStmt`, `DeleteStmt`, `CreateTableStmt`, `ColumnFormatStmt`, `DescribeStmt` and `CommentStmt`. `std::visit` picks the matching `Session::run()` overload, so adding a command means adding one struct, one parser function and one `run()`.

### What a SELECT does, step by step

1. The parser builds a `SelectStmt`: the list of items (`*`, column names, `'literals'`), the table name, and the optional `WHERE`.
2. `Catalog::load()` reads `SYSTEM_TABLE`, and `catalog.table("EMPLOY")` returns its `TableSchema`. An unknown table or column raises a `SqlError`.
3. Each item becomes an output column with a display width: the `COLUMN ... FORMAT` width if one was set, otherwise the column width, widened if needed to fit the column name.
4. `TableFile::readRows()` cuts every line of `EMPLOY_DATA` into one value per column, from `FROM` to `TO` inclusive.
5. Rows that fail the `WHERE` test are skipped. Numbers compare as numbers (`ID <= 3`), text as text.
6. The result is a `ResultSet` (headers, widths, rows). `Output.cpp` renders it as console text, or as JSON for the browser page.

INSERT works the other way round. When the table's first column is ID and one value is missing, the next ID (highest + 1) is put in front. Each value is checked against its column width and placed at its position in a line of spaces. `TableFile::append()` then adds the line to the end of the file.

DELETE and UPDATE share one helper, `TableFile::rewrite()`. It reads every line and asks, for each one, whether to keep it, remove it (DELETE) or edit it in place (UPDATE). It writes the result to `<TABLE>_DATA.tmp`, then renames that file over the original. If the program stops half-way, the original file is still complete. UPDATE checks every new value against its column width before touching the file. Neither statement can change `SYSTEM_TABLE`, and UPDATE cannot change the ID.

CREATE TABLE checks everything first: the names (letters, digits, `_` and `-`, starting with a letter, up to 20 characters, stored in upper case), no duplicate table or column, an `ID` column only in first place, and a row length the catalogue can hold. It then works out each column's `FROM` and `TO` (one after the other, from 0), creates the empty `<TABLE>_DATA` file, and adds all the table's lines to `SYSTEM_TABLE` in one write (`TableFile::appendRows()`). If that write fails, the data file is removed again, so a table is either fully created or not at all. If a `<TABLE>_DATA` file already exists, nothing is created: the statement never adopts or overwrites a file it did not make.

## Storage format and data structures

A table is a text file with one line per row. Each column always sits at the same character positions, padded with spaces. There are no separators, types or indexes. The column positions are recorded in `SYSTEM_TABLE`.

### The catalogue: SYSTEM_TABLE

Each line is 80 characters long and describes one column. `SYSTEM_TABLE` describes itself in its first nine lines:

| Field | Chars | Meaning |
| --- | --- | --- |
| ID | 0–9 | Line number |
| PROGR | 10–14 | Column number within its table |
| TABLE | 15–34 | Table name |
| FIELD | 35–54 | Column name |
| TYPE | 55–59 | `N1` or `S1`, recorded but never used |
| FROM | 60–64 | First character of the column in a data row (counted from 0) |
| TO | 65–69 | Last character of the column |
| LEN | 70–74 | Column width |
| RECLEN | 75–79 | Length of a full row |

Example: the EMPLOY column CITTA occupies characters 170–249 of a 290-character row.

```
14        5    EMPLOY              CITTA               S1   170  249  80   290
```

To add a new table, use `CREATE TABLE` (or **+ New table** on the web page). By hand, the same is done by adding its lines to `data/SYSTEM_TABLE` and creating an empty `data/<TABLE>_DATA` file. If the first column is called ID, `INSERT` fills it in, starting from 1.

One exception: if the table is called `SYSTEM_TABLE`, `SELECT` reads the file `SYSTEM_TABLE` itself. For every other table it reads `<TABLE>_DATA`.

### In memory

Nothing is kept between statements except the `COLUMN` widths. Each statement reloads `SYSTEM_TABLE` and reads the data file it needs, so changes to the files show up at once, even while the server runs.

| Type | Holds |
| --- | --- |
| `Catalog`, `TableSchema`, `Column` | The table definitions: one `Column{name, type, from, to}` per column |
| `Row` | `std::vector<std::string>`, one value per column, trailing spaces removed |
| `Statement` | `std::variant<SelectStmt, InsertStmt, UpdateStmt, DeleteStmt, ColumnFormatStmt, DescribeStmt, CommentStmt>` |
| `StatementResult`, `ResultSet` | Headers, display widths and rows, plus a message or an error |

Problems are thrown as `SqlError`. `Session::execute()` catches them and returns a failed result, so one bad statement never stops a script or the server.

## Building

The build needs `make` and g++ 13 or newer, because the code uses C++20 and `std::format`. Both are installed on this machine: g++ 15.2 and GNU Make 4.4.1.

To get the code on another computer:

```bash
git clone https://github.com/ricard1406/myTinySql.git
```

```bash
cd ~/Projects/myTinySQL
make          # builds ./myTinySql; object files go to build/
make test     # runs the 7 test scripts on a copy of tests/data/
make doxygen  # HTML documentation of the code, from its Doxygen comments
make clean    # removes build/ and the binary
```

The Makefile compiles every `src/*.cpp` with `-std=c++20 -Wall -Wextra -Wpedantic -O2 -pthread`, looking for headers in `include/` (`-Iinclude`). The build has no warnings. It tracks header dependencies (`-MMD`), so editing a `.hpp` file rebuilds every file that includes it.

**Tests:** each `tests/NAME.sql` runs on a fresh copy of `tests/data/`, a frozen set of the tables (so your own changes to `data/` don't affect the tests), and its output must match `tests/NAME.expected` exactly. If you change the output on purpose, run `tests/run_tests.sh --update` and check the diff of the `.expected` files. `make test` is one of twelve checks in [TESTING.md](../TESTING.md), which also covers the browser server, parallel writes, memory safety and a fresh install, each with the command to run and the result to expect.

Some of those checks need tools that a fresh Ubuntu does not have: `curl` (to send requests to the server from the terminal), `git` (for the fresh-install check) and `doxygen` (for the documentation check). Install them with:

```bash
sudo apt install curl git doxygen
```

Run all the checks in the **same terminal window**: they share a temporary copy of the tables, which exists only in the window where you created it. In another window the test server cannot start, and `curl` prints `000` (no connection). [TESTING.md](../TESTING.md) shows how to spot this and what to do.

**Code documentation:** every function, method and declaration has a Doxygen comment (`/// @brief`, `@param`, `@return`, `@throws`). `make doxygen` turns them into HTML pages in `build/doxygen/html/`; open `index.html` in a browser. On GitHub, the workflow in `.github/workflows/doxygen.yml` does the same after every push and publishes the pages at https://ricard1406.github.io/myTinySql/. The settings are in `Doxyfile`, and Doxygen reports any declaration left undocumented (there are none today).

Tables are read from `data/`, relative to the current directory. Use `--data=DIR` to point somewhere else.

## Usage

### Running it

| Mode | Command | Notes |
| --- | --- | --- |
| Script | `./myTinySql --sqlfile=examples/select.sql` | `--sqlfile=-` reads standard input. The exit code is 1 if any statement failed. |
| Interactive | `./myTinySql` | A `tinysql>` prompt; one or more statements per line; Ctrl+D quits |
| Browser | `./myTinySql --http` | Open http://localhost:8080: the tables, a search, add/edit/delete on the rows, and an SQL console |

Other options: `--port=N`, `--data=DIR`, `--web=DIR` (where the page's files are; default `web`), `--help`, and `--allow=LIST`, a comma-separated list of client IP prefixes the server accepts, for example `--allow=127.,192.168.` for your own machine and home network. Without it, the server accepts everyone.

### The web page

`./myTinySql --http`, then open http://localhost:8080.

| Part of the page | What it does | SQL it sends |
| --- | --- | --- |
| **Tables** (on the left) | Click a table to show its rows | `SELECT * FROM <table>` |
| **Search in** + text + **Search** | The rows whose chosen column contains the text, ignoring case; **Show all** clears the search | `SELECT * FROM <table> WHERE <col> LIKE '%text%'` |
| **+ New row** | A form with one field per column (the ID is assigned automatically); **Add row** saves it | `INSERT INTO <table> VALUES ('…', '…')` |
| **Edit** on a row | The cells become input boxes; **Save** (or Enter) sends only the changed columns, **Cancel** (or Escape) sends nothing | `UPDATE <table> SET <col> = '…' WHERE ID = <n>` |
| **Delete** on a row | Asks first, in a dialog | `DELETE FROM <table> WHERE ID = <n>` |
| **+ New table** (under the tables) | A form: the table name, then one line per column with its name, type (Text or Number) and width in characters; **+ Add column** adds a line. **Number the rows automatically** (ticked) puts an ID column first, which Edit and Delete need. The SQL is shown while you type; **Create table** sends it and opens the new, empty table. An error (e.g. the name is taken) appears in the form | `CREATE TABLE <table> (ID NUMBER(10), <col> CHAR(<n>), …)` |
| **SQL console** (at the bottom) | Runs any statements (button or Ctrl+Enter); results appear newest first | What you type |
| **⚙ Settings** (top right) | Options for the page, saved in your browser. **Text size**: Small, Normal, Large or Extra large; it scales the text, the buttons and the spacing | Nothing: settings only change the page |

Above the rows, the page shows the SQL the last action sent and the server's answer (✓ in green, ✗ in red). It remembers the last table, follows the computer's light or dark theme, and also works on a phone. `SYSTEM_TABLE` is read-only here: no **+ New row** and no Edit/Delete.

**How the page works.** The page is three files in `web/`: `index.html` (the structure), `style.css` (the look) and `app.js` (the behaviour). They are plain HTML, CSS and JavaScript, with no libraries and no build step: change a file and reload the page. The page talks to the server through a small API, and updates itself without reloading:

| Request | Answer (JSON) |
| --- | --- |
| `GET /api/tables` | Every table and its columns, e.g. `{"tables":[{"name":"EMPLOY","readOnly":false,"autoId":true,"columns":[{"name":"ID","width":10}, …]}, …]}` |
| `POST /api/sql`, with the SQL as the request body | One result per statement, e.g. `{"results":[{"sql":"DELETE FROM EMPLOY WHERE ID = 3","ok":true,"message":"1 row deleted from EMPLOY","table":"EMPLOY"}]}`; a SELECT also has `columns`, `rows` and `idColumn` |

Values from the server are always shown as plain text (`textContent` in `app.js`), never as HTML, so data such as `<b>` can never change the page. The server only serves plain file names from `web/`, so no request can read a file outside it.

### How the server handles connections

Each browser connection is served in its own thread, so one slow or silent connection never makes the others wait. Browsers such as Chromium open connections early ("preconnect") and may send nothing on them for a while; the server waits up to 30 seconds and then closes such a connection without answering, and the browser simply opens a new one. Running the SQL is still one request at a time: a `std::mutex` makes sure two requests never change the same file together.

### Commands

Keywords and table and column names can be upper or lower case. Write one statement per line, or several separated by `;`. In `INSERT`, values with spaces need no quotes (`Via Garibaldi 3`). In `WHERE` they do: `WHERE CITTA = 'Reggio Emilia'`.

| Command | Syntax |
| --- | --- |
| SELECT | `SELECT <col>,<col>,... FROM <TABLE>` or `SELECT * FROM <TABLE>`. A `'quoted text'` in the list is printed on every row. |
| WHERE | `... WHERE <col> <op> <value>` with `=` `!=` `<>` `<` `<=` `>` `>=`, on any column of the table. Whole numbers compare as numbers; text is case-sensitive. `WHERE <col> LIKE '<pattern>'` searches text, ignoring case: `%` is any text, `_` one character, e.g. `NOME LIKE '%fran%'`. |
| INSERT | `INSERT INTO <TABLE> VALUES (v1,v2,...)`. Leave out the ID to get the next one, or give an unused ID first. Quote a value that has a comma: `'Rossi, Jr'`. |
| UPDATE | `UPDATE <TABLE> SET <col> = <value>, <col> = <value> WHERE <col> <op> <value>`. The WHERE is required, the ID can't be changed, and a value with spaces needs quotes: `SET VIA = 'Via Roma 1'`. |
| DELETE | `DELETE FROM <TABLE> WHERE <col> <op> <value>`. The WHERE is required, so a whole table can't be emptied by mistake. `SYSTEM_TABLE` can't be changed this way. |
| CREATE TABLE | `CREATE TABLE <TABLE> (<col> <type>(<width>), ...)`, where `<type>` is `CHAR` (text) or `NUMBER`, and `<width>` the number of characters. E.g. `CREATE TABLE CARS (ID NUMBER(10), MODEL CHAR(20), YEAR NUMBER(4))`. Put `ID` first to have rows numbered automatically. Adds the lines to `SYSTEM_TABLE` and creates an empty `CARS_DATA`. |
| COLUMN | `COLUMN <col> FORMAT A<n>`: console display width, for the rest of the script, session or request |
| DESCRIBE | `DESCRIBE` lists the tables; `DESCRIBE <TABLE>` lists the columns and their positions |
| Comments | `REM ...` or `-- ...` |

### Example session

`./myTinySql --sqlfile=examples/select.sql` (first two statements; the file also sets widths with `COLUMN`):

```
> SELECT ID,NOME,COGNOME,CITTA FROM EMPLOY WHERE CITTA = Torino
ID-------- NOME-------- COGNOME------------- CITTA---------
2          Luigi        Rossi                Torino
5          Alfredo      Casella              Torino
21         Felice       Giardini             Torino
(3 rows)

> SELECT NOME,COGNOME,'lives in',CITTA FROM EMPLOY WHERE ID <= 5
NOME-------- COGNOME------------- lives in CITTA---------
Giuseppe     Verdi                lives in Milano
Luigi        Rossi                lives in Torino
Pietro       Bianchi              lives in Verona
Ferdinando   Carulli              lives in Napoli
Alfredo      Casella              lives in Torino
(5 rows)
```

Text comparison with `=` is case-sensitive: `WHERE CITTA = torino` finds no rows. `WHERE CITTA LIKE 'torino'` finds the same 3 rows as above, because `LIKE` ignores case.

A mistake prints an error and the script goes on:

```
> SELECT FOO FROM EMPLOY
ERROR: unknown column 'FOO' in table EMPLOY
```

The same kind of query from a terminal, through the page's API (the answer is JSON):

```bash
curl --data-binary "SELECT ID,NOME,CITTA FROM EMPLOY WHERE CITTA = Napoli" http://localhost:8080/api/sql
```
