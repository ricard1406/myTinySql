# myTinySQL

A tiny SQL interpreter over fixed-width text files, written in C++20 as a learning project.

```bash
make                                        # builds ./myTinySql (needs g++ 13 or newer)
make test                                   # runs tests/*.sql and compares with tests/*.expected
make doxygen                                # HTML docs of the code in build/doxygen/html/ (needs doxygen)
./myTinySql --sqlfile=examples/select.sql   # run a script
./myTinySql                                 # type statements interactively
./myTinySql --http                          # browser UI on http://localhost:8080 (show / add / edit / delete rows)
./myTinySql --help                          # all options
```

Supported statements:

```sql
SELECT ID,NOME,'lives in',CITTA FROM EMPLOY WHERE CITTA = Torino
SELECT * FROM CAL_WEEKLY WHERE CALORIES >= 15000
INSERT INTO EMPLOY VALUES (Mario,Rossi,Via Garibaldi,Bologna)
UPDATE EMPLOY SET CITTA = 'Reggio Emilia' WHERE ID = 2
DELETE FROM EMPLOY WHERE ID = 69
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
| `docs/` | Study notes: project guide, old vs new code, `Statement.hpp` explained (start at `docs/README.md`) |

To add a table, add one 80-character line per column to `data/SYSTEM_TABLE`
(same layout as the existing lines) and create an empty `data/<TABLE>_DATA` file.
