# myTinySQL

A tiny SQL interpreter over fixed-width text files, written in C++20 as a learning project.

Code documentation (Doxygen): https://ricard1406.github.io/myTinySql/

**Installation:** requirements, build and first run are in [INSTALLATION.md](INSTALLATION.md).

**Testing:** how to check, step by step, that everything works is in [TESTING.md](TESTING.md);
`make check` runs all those checks at once.

> **DISCLAIMER: All data in this project is sample data, for testing purposes only.
> The companies, addresses and other values are fictional or AI generated. Many names in the
> `EMPLOY` table are those of historical Italian composers, used only as sample values;
> the other values in those rows (such as city and period) are sample data and may not be accurate. Any other resemblance
> to real persons, living or dead, or actual places is purely coincidental.**

```bash
make                                        # builds ./myTinySql (needs g++ 13 or newer)
make test                                   # runs tests/*.sql and compares with tests/*.expected
make check                                  # every check of TESTING.md: ALL DONE - IT WORKS, or OOOPS
make doxygen                                # HTML docs of the code in build/doxygen/html/ (needs doxygen)
./myTinySql --sqlfile=examples/select.sql   # run a script
./myTinySql                                 # type statements interactively
./myTinySql --http                          # browser UI on http://localhost:8080 (show / add / edit / delete rows, create tables)
./myTinySql --help                          # all options
```

Supported statements:

```sql
SELECT ID,NOME,'lives in',CITTA FROM EMPLOY WHERE CITTA = Torino
SELECT * FROM CAL_WEEKLY WHERE CALORIES >= 15000
SELECT * FROM EMPLOY WHERE NOME LIKE '%fran%'
INSERT INTO EMPLOY VALUES (Mario,Rossi,Via Garibaldi,Bologna,Moderno)
UPDATE EMPLOY SET CITTA = 'Reggio Emilia' WHERE ID = 2
DELETE FROM EMPLOY WHERE ID = 69
CREATE TABLE CARS (ID NUMBER(10), MODEL CHAR(20), YEAR NUMBER(4))
COLUMN NOME FORMAT A12
DESCRIBE
DESCRIBE EMPLOY
REM a comment
```

## Layout

| Path | What |
| --- | --- |
| `src/` | The interpreter's sources (`.cpp`): parser, session, table files, output, server |
| `include/` | Its headers (`.hpp`): the types and declarations each source file shares |
| `data/` | The database: `SYSTEM_TABLE` (column positions of every table) and one `<TABLE>_DATA` file per table |
| `examples/` | Sample scripts (`insert.sql` adds rows to `data/`) |
| `tests/` | Golden-output tests, run on a frozen copy of the tables in `tests/data/` |
| `web/` | The browser page: `index.html`, `style.css`, `app.js` (plain HTML, CSS and JavaScript) |
| `docs/` | Study notes: project guide, old vs new code, `Statement.hpp` explained (start at `docs/README.md`) |

To add a table, use `CREATE TABLE` (or **+ New table** on the web page). It adds one 80-character
line per column to `data/SYSTEM_TABLE` and creates an empty `data/<TABLE>_DATA` file; you can also
do both by hand.
