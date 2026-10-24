# Testing

This guide shows **why** myTinySQL works, not just that it does. Every check below is a command you can
copy, followed by the result you should see. If you get the same result, you have checked that part yourself.

These are the same checks used while developing the project (on Ubuntu 26.04, with g++ 15.2), and every
command and expected result in this file was run before it was written down.

## Summary

| # | Check | Command | Expected | What it shows |
| --- | --- | --- | --- | --- |
| 1 | Clean build | `make clean && make` | no warnings | The compiler finds nothing suspicious |
| 2 | Automated tests | `make test` | 6 × `PASS` | The SQL gives exactly the expected output |
| 3 | Command line | `echo "..." \| ./myTinySql ...` | rows, errors, exit code | Scripts and error handling work |
| 4 | Browser server | `curl ...` | 200, 404, 405 | The server answers correctly |
| 5 | Idle connection | `exec 3<>/dev/tcp/...` | `200 OK` after 6 s | Browsers that "preconnect" are served |
| 6 | Parallel writes | 20 × `curl` at once | 28 rows, no duplicate ID | Simultaneous requests can't corrupt a table |
| 7 | File integrity | `awk` on `data/` | one length per file | Every row has the exact fixed width |
| 8 | Memory safety | sanitizer build | 0 problems | No memory errors or undefined behaviour |
| 9 | Documentation | `make doxygen` | no warnings | Every declaration is documented |
| 10 | Fresh install | `git clone` + `make test` | 6 × `PASS` | The project works from a clean copy |
| 11 | By hand, in a browser | checklist | as described | The web page works for a person |
| 12 | On GitHub | Actions tab | green check | The online documentation is up to date |

## Before you start: work on a copy

Checks 3 to 8 change tables (they insert, update and delete rows). So that your real tables in `data/`
are never touched, they run on a copy of the frozen test tables in a temporary folder. Run these lines
once, in the project folder, in the same terminal you will use for the checks:

```bash
PROJECT=$(pwd)
TEST_DIR=$(mktemp -d)
cp -r tests/data "$TEST_DIR/data"
```

`PROJECT` remembers the project folder, whatever its name; `TEST_DIR` is a new, empty temporary folder.

Run each block below as it is, one line at a time or all at once. Keep each command on its own line:
a line ending with `&` runs in the background, and joining it to other commands with `&&` sends them all
to the background too.

## 1. Clean build

```bash
make clean && make
```

**Expected:** the compile commands, and no line containing `warning`. To count them:

```bash
make clean && make 2>&1 | grep -c warning
```

```
0
```

**Why it matters:** the Makefile asks for every useful warning (`-Wall -Wextra -Wpedantic`). Warnings often
point at real bugs (an unused result, a wrong type, a missing case), so the project keeps them at zero.

## 2. Automated tests

```bash
make test
```

**Expected:**

```
tests/run_tests.sh
PASS delete
PASS describe
PASS errors
PASS insert
PASS select
PASS update
```

**How it works:** each `tests/NAME.sql` is a script of statements, and `tests/NAME.expected` is the exact
output it must produce. `tests/run_tests.sh` runs every script on a fresh copy of `tests/data/` and compares
the output with `diff`. One different character is enough to fail. Together, the 6 scripts cover SELECT,
WHERE with every operator and `LIKE`, INSERT, UPDATE, DELETE, DESCRIBE, COLUMN, and the error messages.

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

**Expected** (the first lines):

```
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

**Expected:**

```
> SELECT FOO FROM EMPLOY
ERROR: unknown column 'FOO' in table EMPLOY

exit code: 1
```

**What it shows:** errors are reported clearly, the program does not stop, and a script that had a failing
statement ends with exit code 1, so other programs can detect it.

## 4. Browser server

Start a test server in the background, on a spare port and on the copy of the tables:

```bash
./myTinySql --http --port=18080 --data="$TEST_DIR/data" > /dev/null 2>&1 &
SERVER=$!
sleep 1
```

Ask it for the page, a file that does not exist, a request that is not GET, and a query:

```bash
curl -s -o /dev/null -w '%{http_code}\n' http://localhost:18080/
curl -s -o /dev/null -w '%{http_code}\n' http://localhost:18080/favicon.ico
curl -s -o /dev/null -w '%{http_code}\n' -X POST http://localhost:18080/
curl -s 'http://localhost:18080/?subject=DESCRIBE' | grep -o '<p>[0-9]* tables</p>'
```

**Expected:**

```
200
404
405
<p>4 tables</p>
```

`200` means OK, `404` "not found" and `405` "method not allowed". Leave the server running for checks 5 and 6.

## 5. Idle connection ("preconnect")

Browsers such as Chromium open a connection early and may send nothing on it for a while. This opens such a
silent connection, checks that other requests are not blocked by it, then uses it after 6 seconds:

```bash
exec 3<>/dev/tcp/localhost/18080
curl -s -o /dev/null -w 'other request while idle: %{time_total}s\n' http://localhost:18080/
sleep 6
printf 'GET /?subject=DESCRIBE HTTP/1.1\r\nHost: x\r\n\r\n' >&3
head -1 <&3
exec 3<&-
```

**Expected:**

```
other request while idle: 0.000588s
HTTP/1.1 200 OK
```

(The time will differ a little; it must be well under one second.)

**Why this check exists:** an earlier version served one connection at a time and answered a silent
connection with an error after 5 seconds. In Chromium, every click then showed "Only GET requests are
supported". This check reproduces exactly that situation; the server now gives each connection its own thread.

## 6. Parallel writes

Send 20 INSERTs at the same moment, then wait for those 20 requests:

```bash
pids=()
for i in $(seq 1 20); do
    curl -s -o /dev/null "http://localhost:18080/?subject=INSERT+INTO+COMPANY+VALUES+('Firm+$i',Test,'Via+$i',Roma)" &
    pids+=($!)
done
wait "${pids[@]}"
```

Count the rows, the duplicate IDs, and the different row lengths:

```bash
wc -l < "$TEST_DIR/data/COMPANY_DATA"
cut -c1-10 "$TEST_DIR/data/COMPANY_DATA" | sort | uniq -d | wc -l
LC_ALL=C awk '{print length($0)}' "$TEST_DIR/data/COMPANY_DATA" | sort -u
```

**Expected:**

```
28
0
250
```

8 original rows + 20 new ones; no ID used twice; every row exactly 250 characters long. The server reads
requests in parallel, but runs the SQL one request at a time (a `std::mutex`), so no two INSERTs can pick
the same ID or write into each other.

Note `wait "${pids[@]}"`: a plain `wait` would also wait for the server, which never ends.

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

**Expected:** one length per file.

```
CAL_WEEKLY_DATA: 35
COMPANY_DATA: 250
EMPLOY_DATA: 290
SYSTEM_TABLE: 80
```

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

**Expected:**

```
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

**Expected:**

```
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

**Expected:** `0` warnings, then the same 6 `PASS` lines as in check 2.

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
| Open the page | The table list shows EMPLOY; the SQL box contains an example query |
| Click **Run** | A table of 5 rows appears |
| Choose COMPANY, click **Show rows** | The companies (8, plus the 20 test firms if you ran check 6), each row with **Edit** and **Delete** |
| Choose EMPLOY; in **Search**, column NOME, type `fran`, click **Search** | The rows whose NOME contains "fran" (Francesco, Franco); `fran` is still in the box |
| In **Add a row**, fill every field, with an apostrophe in one (`O'Brien`), click **Add row** | "1 row inserted" and the new row, apostrophe included |
| On that row, click **Edit**, change a value, press **Escape** | The row is unchanged and nothing was sent |
| Click **Edit** again, change a value with a space (`Reggio Emilia`), press **Enter** | "1 row updated", the new value shown |
| Click **Delete** on it, then **Cancel** in the dialog | Nothing is deleted |
| Click **Delete** again, then **OK** | "1 row deleted", the row is gone |

Above the results, the page always shows the SQL each button sent: reading it is part of the check.
Stop the server with Ctrl+C.

## 12. On GitHub

After each push to `main`, GitHub rebuilds the documentation site. In the repository's **Actions** tab,
the latest "Doxygen documentation" run must have a green check. From a terminal:

```bash
gh run list --repo ricard1406/myTinySql --limit 1
```

**Expected:** a line starting with `completed	success`.

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
| A plain `wait` in a test script waited forever for the server | Writing check 6 of this guide | Wait only for the 20 requests |

The last one is a reminder that test scripts are code too, and need testing as well.
