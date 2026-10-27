# Testing

This guide shows **why** myTinySQL works, not just that it does. Every check below is a command you can
copy, followed by the result you should see. If you get the same result, you have checked that part yourself.

These are the same checks used while developing the project (on Ubuntu 26.04, with g++ 15.2), and every
command and expected result in this file was run before it was written down.

## All checks at once

One command runs checks 1 to 10 and 12, compares every result with the one this guide expects, and ends
with a single verdict:

```bash
make check
```

**You should see** (output, don't type it) one line per check, then the verdict:

```text
myTinySQL: running the checks of TESTING.md (about 2 minutes)

  1  Clean build              ok
  2  Automated tests          ok   8 test scripts
  3  Command line             ok
  4  Browser server           ok
  5  Idle connection          ok   other request answered in 0.000491s
  6  Parallel writes          ok
  7  File integrity           ok   5 files
  8  Memory safety            ok
  9  Documentation            ok
 10  Fresh install            ok   the version on GitHub, 8 test scripts
 11  By hand, in a browser    --   by a person: see TESTING.md, check 11
 12  On GitHub                ok

ALL DONE - IT WORKS   (11 checks passed)
```

If something is wrong, the verdict is `OOOPS - SOMETHING IS WRONG`, the failed checks say why, and the
script prints where its log is: everything each check printed, with the expected and actual results.
If a tool is missing, it says which one and how to install it, before running anything.

- `make check` is `tests/check_all.sh`; `tests/check_all.sh --offline` skips the two checks that need the
  internet (10 and 12).
- Check 11 needs a person and a browser, so the script only lists it. Check 12 is skipped if `gh` is not
  installed or not logged in, or while the latest run is still going.
- Like the checks below, it works on a temporary copy of the tables: `data/` is only read (check 7).

The rest of this guide is the same checks one by one, with what each one shows and why it exists.

## Summary

| # | Check | Command | Expected | What it shows |
| --- | --- | --- | --- | --- |
| 1 | Clean build | `make clean && make` | no warnings | The compiler finds nothing suspicious |
| 2 | Automated tests | `make test` | 8 × `PASS` | The SQL gives exactly the expected output |
| 3 | Command line | `echo "..." \| ./myTinySql ...` | rows, errors, exit code | Scripts and error handling work |
| 4 | Browser server | `curl ...` | 200, 404, 405 | The server answers correctly |
| 5 | Idle connection | `exec 3<>/dev/tcp/...` | `200 OK` after 6 s | Browsers that "preconnect" are served |
| 6 | Parallel writes | 20 × `curl` at once | 28 rows, no duplicate ID | Simultaneous requests can't corrupt a table |
| 7 | File integrity | `awk` on `data/` | one length per file | Every row has the exact fixed width |
| 8 | Memory safety | sanitizer build | 0 problems | No memory errors or undefined behaviour |
| 9 | Documentation | `make doxygen` | no warnings | Every declaration is documented |
| 10 | Fresh install | `git clone` + `make test` | 8 × `PASS` | The project works from a clean copy |
| 11 | By hand, in a browser | checklist | as described | The web page works for a person |
| 12 | On GitHub | Actions tab | green check | The online documentation is up to date |

## How to read this guide

Each check has two kinds of grey boxes:

- **Commands**: copy them into the terminal and press Enter.
- **Output**: every output box comes right after the words "**You should see**". It shows what the
  commands print. Don't type it: compare it with what your terminal shows. For example, a box containing
  only `0` means the command must print `0`.

## What you need

The build tools from [INSTALLATION.md](INSTALLATION.md), plus the tools some checks use. On Ubuntu:

```bash
sudo apt install curl git doxygen
```

| Tool | Used by | Note |
| --- | --- | --- |
| `curl` | checks 4, 5, 6 | Sends requests to the server from the terminal; not installed on a fresh Ubuntu |
| `git` | check 10 | Downloads a fresh copy of the project |
| `doxygen` | check 9 | Only for the documentation check |
| `gh` (GitHub CLI) | check 12 | Optional: the Actions tab on github.com shows the same thing |
| A web browser | check 11 | E.g. Chromium or Firefox |

To see which tools are missing, run:

```bash
for tool in g++ make curl git doxygen; do command -v $tool > /dev/null || echo "missing: $tool"; done; echo "tool check done"
```

**You should see** (output, don't type it) only `tool check done`. Each `missing:` line names a tool to
install with `sudo apt install` (`g++` and `make` come with `build-essential`).

## Before you start: work on a copy

Checks 3 to 8 change tables (they insert, update and delete rows). So that your real tables in `data/`
are never touched, they run on a copy of the frozen test tables in a temporary folder. Run these lines
once, in the project folder, in the same terminal you will use for the checks:

```bash
PROJECT=$(pwd)
TEST_DIR=$(mktemp -d)
cp -r tests/data "$TEST_DIR/data"
echo "TEST_DIR is: $TEST_DIR"
```

**You should see** (output, don't type it) a temporary folder; the letters after `tmp.` change every time:

```text
TEST_DIR is: /tmp/tmp.KDNZpJ10N7
```

`PROJECT` remembers the project folder, whatever its name; `TEST_DIR` is the temporary folder. Both exist
only in this terminal window: **run all the checks in the same window**. If you close it or open a new
one, run these lines again first.

Run each block below as it is, one line at a time or all at once. Keep each command on its own line:
a line ending with `&` runs in the background, and joining it to other commands with `&&` sends them all
to the background too.

## 1. Clean build

```bash
make clean && make
```

**You should see** the compile commands, and no line containing `warning`. To count those lines, run:

```bash
make clean && make 2>&1 | grep -c warning
```

```text
0
```

**Why it matters:** the Makefile asks for every useful warning (`-Wall -Wextra -Wpedantic`). Warnings often
point at real bugs (an unused result, a wrong type, a missing case), so the project keeps them at zero.

## 2. Automated tests

```bash
make test
```

**You should see** (output, don't type it):

```text
tests/run_tests.sh
PASS apostrophes
PASS create
PASS delete
PASS describe
PASS errors
PASS insert
PASS select
PASS update
```

**How it works:** each `tests/NAME.sql` is a script of statements, and `tests/NAME.expected` is the exact
output it must produce. `tests/run_tests.sh` runs every script on a fresh copy of `tests/data/` and compares
the output with `diff`. One different character is enough to fail. Together, the 8 scripts cover SELECT,
WHERE with every operator and `LIKE`, INSERT, UPDATE, DELETE, CREATE TABLE, DESCRIBE, COLUMN, apostrophes in values
(d'Asti, L'Aquila), and the error messages.

**If a test fails**, the runner prints the difference: lines starting with `-` are what was expected,
lines starting with `+` are what the program printed instead.

**Adding a test:** add statements to a `tests/*.sql` file (or create a new one), then run
`tests/run_tests.sh --update` to record the output, and **read** the new `.expected` file (or
`git diff tests/`) to confirm the output is right before you commit it.

**Why `tests/data/` and not `data/`:** the tests need tables that never change. If they used your working
tables, adding a row on the web page would make the tests fail although nothing is broken. This actually
happened during development, which is why the tests got their own frozen copy.

## 3. Command line

A search with `LIKE`:

```bash
echo "SELECT ID,NOME,COGNOME FROM EMPLOY WHERE NOME LIKE '%fran%'" | ./myTinySql --data="$TEST_DIR/data" --sqlfile=-
```

**You should see** (the first lines; output, don't type it):

```text
> SELECT ID,NOME,COGNOME FROM EMPLOY WHERE NOME LIKE '%fran%'
ID-------- NOME------------------------------------ COGNOME---------------------------------
11         Francesco                                Cavalli
16         Francesco                                Gasparini
17         Francesco                                Geminiani
```

A mistake, and the exit code a script returns when a statement fails:

```bash
echo "SELECT FOO FROM EMPLOY" | ./myTinySql --data="$TEST_DIR/data" --sqlfile=-
echo "exit code: $?"
```

**You should see** (output, don't type it):

```text
> SELECT FOO FROM EMPLOY
ERROR: unknown column 'FOO' in table EMPLOY

exit code: 1
```

**What it shows:** errors are reported clearly, the program does not stop, and a script that had a failing
statement ends with exit code 1, so other programs can detect it.

## 4. Browser server

First make sure that `curl` is installed, and that the copy of the tables from "Before you start" is there,
in this terminal window:

```bash
command -v curl > /dev/null && echo "curl found" || echo "curl is missing: sudo apt install curl"
test -d "$TEST_DIR/data" && echo "test copy found" || echo "TEST_DIR is empty: run the 'Before you start' lines in this window"
```

**You should see** (output, don't type it):

```text
curl found
test copy found
```

Start a test server in the background, on a spare port and on the copy of the tables:

```bash
./myTinySql --http --port=18080 --data="$TEST_DIR/data" > "$TEST_DIR/server.log" 2>&1 &
SERVER=$!
sleep 1
if kill -0 $SERVER 2>/dev/null; then echo "server is running"; else echo "server did not start:"; cat "$TEST_DIR/server.log"; fi
```

**You should see** (output, don't type it):

```text
server is running
```

If it says "server did not start", the line after it tells you why; see the note at the end of this check.

The terminal prints a line like `[1] 12345`: that is the background job's number and process ID, and it
is normal. When you stop the server at the end of check 6, it prints a line like `[1]+  Terminated` (in
Italian, `Terminato`): also normal.

Ask it for the page, a file that does not exist, a method it does not support, the list of tables,
a statement, and a file outside the page's folder:

```bash
curl -s -o /dev/null -w '%{http_code}\n' http://localhost:18080/
curl -s -o /dev/null -w '%{http_code}\n' http://localhost:18080/favicon.ico
curl -s -o /dev/null -w '%{http_code}\n' -X PUT http://localhost:18080/api/sql
curl -s -o /dev/null -w '%{http_code} %{content_type}\n' http://localhost:18080/api/tables
curl -s --data-binary 'DESCRIBE' http://localhost:18080/api/sql | grep -o '"message":"[^"]*"'
curl -s -o /dev/null -w '%{http_code}\n' --path-as-is http://localhost:18080/../data/SYSTEM_TABLE
```

**You should see** (output, don't type it):

```text
200
404
405
200 application/json
"message":"4 tables"
404
```

`200` means OK, `404` "not found" and `405` "method not allowed". The page itself is `index.html` from the
`web/` folder; `/api/tables` and `/api/sql` are the API the page uses, and they answer in JSON. The last
line tries to read a file outside `web/` (`--path-as-is` makes `curl` send the `..` exactly as typed): the
server refuses, so nothing outside the page's folder can be read. Leave the server running for checks 5 and 6.

**If `curl` prints `000`:** it could not connect, so no server is running on port 18080. Usually the server
stopped right after starting. The most common reasons:

| Message in "server did not start" | Cause | Fix |
| --- | --- | --- |
| `bash: /server.log: Permission denied` | `TEST_DIR` is empty: the "Before you start" lines ran in another terminal window | Run them again in this window, then restart this check |
| `bind to port 18080: Address already in use` | Another server (perhaps from an earlier try) still uses the port | Stop it (`kill %1`, or close that terminal), then restart this check |

## 5. Idle connection ("preconnect")

Browsers such as Chromium open a connection early and may send nothing on it for a while. This opens such a
silent connection, checks that other requests are not blocked by it, then uses it after 6 seconds:

```bash
exec 3<>/dev/tcp/localhost/18080
curl -s -o /dev/null -w 'other request while idle: %{time_total}s\n' http://localhost:18080/
sleep 6
printf 'GET /api/tables HTTP/1.1\r\nHost: x\r\n\r\n' >&3
head -1 <&3
exec 3<&-
```

**You should see** (output, don't type it):

```text
other request while idle: 0.000588s
HTTP/1.1 200 OK
```

(The time will differ a little; it must be well under one second.)

**Why this check exists:** an earlier version served one connection at a time and answered a silent
connection with an error after 5 seconds. In Chromium, every click then showed "Only GET requests are
supported". This check reproduces exactly that situation; the server now gives each connection its own thread.

## 6. Parallel writes

Send 20 INSERTs at the same moment, and wait until all 20 are answered:

```bash
(
    for i in $(seq 1 20); do
        curl -s -o /dev/null --data-binary "INSERT INTO COMPANY VALUES ('Firm $i',Test,'Via $i',Roma)" http://localhost:18080/api/sql &
    done
    wait
)
```

The parentheses run the loop in a *subshell*, a separate copy of the shell. That does two things: the
terminal doesn't announce each of the 20 background requests, and `wait` waits only for those requests,
not for the server (which never ends).

Count the rows, the duplicate IDs, and the different row lengths:

```bash
wc -l < "$TEST_DIR/data/COMPANY_DATA"
cut -c1-10 "$TEST_DIR/data/COMPANY_DATA" | sort | uniq -d | wc -l
LC_ALL=C awk '{print length($0)}' "$TEST_DIR/data/COMPANY_DATA" | sort -u
```

**You should see** (output, don't type it):

```text
28
0
250
```

8 original rows + 20 new ones; no ID used twice; every row exactly 250 characters long. The server reads
requests in parallel, but runs the SQL one request at a time (a `std::mutex`), so no two INSERTs can pick
the same ID or write into each other.

Stop the test server:

```bash
kill $SERVER
```

## 7. File integrity

Every table file must have rows of one exact length (the one in `SYSTEM_TABLE`). This reads your real
tables, without changing them:

```bash
for f in data/*; do echo "$(basename $f): $(LC_ALL=C awk '{print length($0)}' "$f" | sort -u | tr '\n' ' ')"; done
```

**You should see** one length per file (output, don't type it):

```text
CAL_WEEKLY_DATA: 35
COMPANY_DATA: 250
EMPLOY_DATA: 290
SYSTEM_TABLE: 80
VINI-ITALY_DATA: 180
```

(Your list may differ if you created other tables: what matters is one length per file. `make check`
also compares each length with the row length in `SYSTEM_TABLE`.)

`LC_ALL=C` makes `awk` count bytes, as the program does: an accented letter such as `ò` is 2 bytes.

## 8. Memory safety (sanitizers)

C++ does not check memory by itself. The compiler can add those checks: AddressSanitizer catches reads and
writes outside memory you own, and UndefinedBehaviorSanitizer catches operations C++ leaves undefined
(overflows, bad casts). Build a checked copy of the program, then run every test script with it:

```bash
g++ -std=c++20 -g -fsanitize=address,undefined -pthread -Iinclude -o build/myTinySql-asan src/*.cpp
for f in tests/*.sql; do
    rm -rf "$TEST_DIR/asan" && cp -r tests/data "$TEST_DIR/asan"
    build/myTinySql-asan --data="$TEST_DIR/asan" --sqlfile="$f" > /dev/null 2> "$TEST_DIR/asan.log"
    echo "$f: $(grep -c -E 'Sanitizer|runtime error' "$TEST_DIR/asan.log") problems"
done
```

**You should see** (output, don't type it):

```text
tests/apostrophes.sql: 0 problems
tests/create.sql: 0 problems
tests/delete.sql: 0 problems
tests/describe.sql: 0 problems
tests/errors.sql: 0 problems
tests/insert.sql: 0 problems
tests/select.sql: 0 problems
tests/update.sql: 0 problems
```

The checked program is slower and bigger, so it is only for testing. It stays in `build/`, which `make clean` removes.

## 9. Documentation coverage

```bash
make doxygen 2>&1 | grep -c warning
```

**You should see** (output, don't type it):

```text
0
```

Doxygen warns about every function, parameter or return value without a comment, so 0 warnings means the
whole code is documented. (It needs Doxygen; see [INSTALLATION.md](INSTALLATION.md).)

## 10. Fresh install

The checks above use your working folder. This one checks that the project also works for someone who
downloads it for the first time:

```bash
cd "$TEST_DIR"
git clone -q https://github.com/ricard1406/myTinySql.git
cd myTinySql
make 2>&1 | grep -c warning
make test
cd "$PROJECT"
```

**You should see** `0`, then the same 8 `PASS` lines as in check 2.

**Why this check exists:** a fresh copy has no `build/` folder and nothing left over from earlier work.
That is exactly how a bug in `make doxygen` was found: it worked on the developer's machine, where
`build/` already existed, and failed on a clean one.

## 11. By hand, in a browser

Start the server on the copy of the tables, then open http://localhost:18080 in a browser
(tested with Chromium and Firefox):

```bash
./myTinySql --http --port=18080 --data="$TEST_DIR/data"
```

| Step | Expected |
| --- | --- |
| Open the page | The tables on the left; EMPLOY (or the last table you used) with its rows |
| Click **COMPANY** on the left | The companies (8, plus the 20 test firms if you ran check 6), each row with **Edit** and **Delete** |
| Click **SYSTEM_TABLE** | Its rows, with no **+ New row** and no Edit/Delete: the catalogue is read-only |
| Click **EMPLOY**; in **Search in** keep NOME, type `fran`, click **Search** | The rows whose NOME contains "fran" (Francesco, Franco) |
| Click **+ New row**, fill every field, with an apostrophe in one (``D'Annunzio``), click **Add row** | A "1 row inserted" message; the form closes and the new row is the last one, apostrophe included. The SQL above the rows shows ``'D'Annunzio'`` as typed |
| On that row, click **Edit**, change a value, press **Escape** | The row is unchanged, and the SQL line above the rows still shows the INSERT: nothing was sent |
| Click **Edit** again, change a value to one with a space (`Reggio Emilia`), press **Enter** | "1 row updated", the new value shown |
| Click **Delete** on it, then **Cancel** in the dialog | Nothing is deleted |
| Click **Delete** again, then the red **Delete** | "1 row deleted", the row is gone |
| In the **SQL console**, type `SELECT * FROM COMPANY WHERE TOWN = Roma` and, on a new line, `SELECT FOO FROM COMPANY`; press Ctrl+Enter | Two results: the rows with a green check, and the error in red |
| Click **+ New table**; name `cars`; first column `model`, Text, 20; click **+ Add column**: `year`, Number, 4 | The SQL under the columns reads `CREATE TABLE CARS (ID NUMBER(10), MODEL CHAR(20), YEAR NUMBER(4))` while you type |
| Click **Create table** | "table CARS created with 3 columns (row length 34)"; CARS appears on the left, open and empty, with **+ New row** |
| Click **+ New table** again, name `cars`, any column, **Create table** | The form stays open and shows "table CARS already exists" in red; click **Cancel** |
| Click **⚙ Settings**, choose **Extra large**, click **Done**, then reload the page | The whole page (text, buttons, spacing) is bigger, and still is after the reload; set it back to **Normal** |
| Switch your computer to a dark theme (optional) | The page follows it, with dark colours |

Above the rows, the page always shows the SQL the last action sent and the server's answer: reading it is
part of the check. Stop the server with Ctrl+C.

## 12. On GitHub

After each push to `main`, GitHub rebuilds the documentation site. In the repository's **Actions** tab,
the latest "Doxygen documentation" run must have a green check. From a terminal:

```bash
gh run list --repo ricard1406/myTinySql --limit 1
```

**You should see** (output, don't type it) a small table whose first row starts with a green check mark:

```text
STATUS  TITLE                     WORKFLOW               BRANCH  EVENT  ID           ELAPSED  AGE
✓       Docs: link TESTING.md...  Doxygen documentation  main    push   40098806...  53s      about 3 minutes ago
```

A red `X` instead of the check mark means the run failed: click it in the Actions tab to see why.

## Clean up

Remove the temporary copy and everything in it:

```bash
rm -rf "$TEST_DIR"
```

## When to run what

| After changing... | Run checks |
| --- | --- |
| Any code | 1, 2 (and 8 for anything that handles memory or text) |
| `Server.cpp` or `Output.cpp` | 1, 2, 4, 5, 6, 11 |
| The tables in `data/` | 7 |
| Comments in the code | 9 |
| The Makefile, or before a release | all of them |

## Bugs these checks have caught

| Bug | Found by | Fixed |
| --- | --- | --- |
| The last character of every column was lost (`13743` shown as `1374`) | Comparing real output with the data (now check 2) | Columns read from FROM to TO inclusive |
| Chromium showed "Only GET requests are supported" on every click | A browser test, then reproduced by check 5 | One thread per connection; silent connections closed without a reply |
| The tests failed after a row was added on the web page | Check 2 | Tests use their own frozen tables in `tests/data/` |
| `make doxygen` failed on a clean machine (GitHub) | Check 12, then check 10 | `make doxygen` creates its output folder first |
| A plain `wait` in a test script waited forever for the server | Writing check 6 of this guide | Run the 20 requests in a subshell, whose `wait` ignores the server |
| On a fresh Ubuntu: `curl` missing, and an output box (`0`) typed as a command | A user following this guide | "What you need" section; output boxes labelled "You should see" |
| On a fresh Ubuntu 26.04: `curl` still missing at check 4 | A user following this guide | A one-line tool check in "What you need", and a `curl` check at the start of check 4 |

The last one is a reminder that test scripts are code too, and need testing as well.
