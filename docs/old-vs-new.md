# From the 2023 code to the new code

A summary to help you study `src/`. The 2023 files are in `legacy/`.

## Where each old piece went

| 2023 (`legacy/`) | New (`src/`) | What changed |
| --- | --- | --- |
| `myTinySqlMain.cpp`: `main()` plus two copy-pasted socket servers | `main.cpp` and `Server.cpp` | One server serves both modes; a setting chooses HTTP or Android |
| `myTinySql.cpp`: `SqlStatemen()`, a chain of `if` checks on the first word | `Parser.cpp` and `Session.cpp` | The text is parsed first, then run |
| `myTinySELECT.cpp`, `myTinyINSERT.cpp`, `myTinyCOLUMN.cpp`, `myTinyDESCRIBE.cpp` | One `run()` per statement type in `Session.cpp` | Each `run()` is about 20–70 lines, and none prints anything itself |
| `myToolBox.cpp` (1129 lines doing everything) | Split into `Catalog`, `TableFile`, `Output`, `Util` | Each file has one job |
| `myPublicVar.h`: about 25 global variables and `#define`s | Gone | State lives in objects (`Session`, `Catalog`) |
| `mySelectPrintResult()` | `Output.cpp` | Rendering is separate from running |

## The main ideas that changed

1. **Global buffers became values.** Old: `TableHeaderM`, `QueryPropertiesM` and `Table2Print` were `calloc` arrays addressed as `row * width + col`. New: `std::vector<Column>`, `std::vector<Row>`, `ResultSet`. They have no fixed size and nothing to `free()`.

2. **Word positions became a real parser.** Old: "the table name is word 3", so a missing or extra space broke the query. New: `tokenize()` splits the text into tokens (words, `'strings'`, symbols), and a `Parser` reads them following the grammar. The result is a typed `SelectStmt` or `InsertStmt`.

3. **The `if` chain became `std::variant` plus `std::visit`.**
   ```cpp
   using Statement = std::variant<SelectStmt, InsertStmt, UpdateStmt, DeleteStmt, ColumnFormatStmt, DescribeStmt, CommentStmt>;
   std::visit([this](const auto& stmt) { return run(stmt); }, statement);
   ```
   The compiler picks the right `run()` overload. If you add a statement type and forget its `run()`, the build fails.

4. **`exit()` became exceptions.** Old: `ErrorExit()` stopped the whole program, server included. New: any problem `throw`s a `SqlError`, `Session::execute()` catches it and returns a failed result, and the next statement runs.

5. **Running is separate from printing.** Old: SELECT filled `Table2Print`, then printed it in three different ways. New: `Session` returns plain data (`StatementResult`), and `Output.cpp` turns it into text, HTML or Android tags. This makes each part easy to test on its own.

6. **Fixed offsets became a catalogue loaded at run time.** Old: `SetConstants()` hard-coded the field positions before every statement. New: `Catalog::load()` reads `SYSTEM_TABLE` into `TableSchema` / `Column{name, type, from, to}`. Only the catalogue's own layout is still fixed (`kTable{15, 34}` and so on).

7. **Resources clean themselves up (RAII).** A `Socket` class closes its descriptor in its destructor, and files are `std::ifstream` / `std::ofstream`. There is no manual `close()` or `free()`.

8. **No threads.** Old: each request started a `std::thread` and immediately `join()`ed it, so it gained nothing. New: requests are handled one at a time, which also means two INSERTs from the server can't race each other.

9. **Modern C++ features to look for:** `std::string_view` (strings read without copying), `std::optional` (a value that may be missing, like `indexOf()`), `std::format`, designated initializers (`Column{.name = ..., .from = ...}`), `std::ranges::find_if`, and the `<=>` comparison in `matches()`.

10. **Tests.** `make test` runs each `tests/*.sql` script on a copy of `data/` and compares the output with the matching `.expected` file. Before, the only way to check anything was to run it by hand.

## Suggested reading order

1. `Statement.hpp`: the data types; the rest of the code is built around them. See [statement-hpp-explained.md](statement-hpp-explained.md).
2. `Parser.cpp`: start with `tokenize()`, then `parseSelect()`.
3. `Session.cpp`: `execute()`, then `run(const SelectStmt&)`.
4. `Catalog.cpp` and `TableFile.cpp`: how the fixed-width files are read.
5. `Output.cpp`, then `main.cpp`.
6. `Server.cpp` last; it is mostly POSIX socket code.

While you read, it helps to run `./myTinySql`, type a statement, and follow it through each file.
