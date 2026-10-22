# `include/Statement.hpp` explained, with a comparison to C

`Statement.hpp` holds **pure data**: it describes what a parsed statement looks like. It contains no logic except two function declarations. Everything else in the project (parser, session, output) is built around these types, which is why it's the best file to study first.

Below, each declaration is followed by how you would write the same thing in plain C.

---

## 1. The header guard and the namespace

```cpp
#pragma once
namespace tinysql { ... }
```

- `#pragma once` tells the compiler to include this file only once, even if several files include it.
- `namespace tinysql` puts every name inside `tinysql::`. That way `Statement` can't clash with a `Statement` from another library.

**In C:**
```c
#ifndef TINYSQL_STATEMENT_H
#define TINYSQL_STATEMENT_H
/* ... */
#endif
```
C has no namespaces, so you add a prefix to every name by hand, e.g. `tinysql_statement`. The 2023 headers (`myToolBox.h`, `myPublicVar.h`) had no guard at all, so including one twice would break the build.

---

## 2. `SelectItem`: one entry of the SELECT list

```cpp
/// One entry of a SELECT list: `*`, a column name, or a 'quoted literal' printed on every row.
struct SelectItem {
    enum class Kind { Star, Column, Literal };
    Kind kind;
    std::string text;
};
```

- `///` marks a documentation comment. Tools like Doxygen and most editors show it when you hover the name.
- `enum class` is a *scoped* enum. You write `SelectItem::Kind::Star`, never just `Star`, and it doesn't silently convert to `int`.
- `std::string text` owns its memory: it grows as needed and frees itself.

`SELECT ID,'lives in',* FROM ...` becomes three items: `{Column,"ID"}`, `{Literal,"lives in"}`, `{Star,"*"}`.

**In C:**
```c
typedef enum { ITEM_STAR, ITEM_COLUMN, ITEM_LITERAL } ItemKind;  /* plain ints, global names */

typedef struct {
    ItemKind kind;
    char    *text;   /* malloc'd: someone must remember to free() it */
} SelectItem;
```
`ITEM_STAR` is just the number 0, and you could assign 42 to `kind` without any warning. You also have to decide who frees `text`, and remember to do it.

---

## 3. `Condition`: a small group of values

```cpp
/// WHERE <column> <op> <value>, with op one of = != <> < <= > >=
struct Condition {
    std::string column;
    std::string op;
    std::string value;
};
```
This is just a named group of values. In C it would need either fixed buffers (`char column[21]; char op[3]; char value[81];`, which risk overflow) or three `malloc`'d pointers.

---

## 4. `SelectStmt`: a whole parsed SELECT

```cpp
struct SelectStmt {
    std::vector<SelectItem> items;            // any number of items
    std::string table;
    std::optional<Condition> where;           // there may be no WHERE
};
```

(Until 2026-10-22 it also had two fields for the Android clauses, `ANDROID-LIST` and `ANDROID-DISTINCT`. They were removed together with the unused Android mode.)

- `std::vector<SelectItem>` is a growable array that knows its own length.
- `std::optional<Condition>` means "a Condition, or nothing". You test it with `if (stmt.where)` and use it as `stmt.where->column`. The type itself says the WHERE is optional.
- Every member here has a type that starts out empty (an empty vector, an empty string, no condition), so a new `SelectStmt` never contains garbage. For a plain type such as `bool` you write a *default member initializer* yourself, as `Session.hpp` does: `bool failed = false;`.

**In C:**
```c
typedef struct {
    SelectItem *items;          /* malloc'd array ...           */
    size_t      itemCount;      /* ... its length kept apart    */
    char       *table;
    int         hasWhere;       /* "optional" by convention:    */
    Condition   where;          /* only valid if hasWhere != 0  */
} SelectStmt;

SelectStmt s = {0};   /* you must remember to zero it */
```
In C, each optional needs an extra `has...` flag, and nothing stops you from reading `where` when `hasWhere` is 0. The array and its length live separately and can get out of sync.

**In the 2023 code** there was no such struct. Each handler went back to the raw text every time it needed a piece, e.g. `SqlGetOneWord(line, ' ', 3)` for "the 4th word is the table". That is why one extra space broke a query.

---

## 5. `InsertStmt`, `ColumnFormatStmt`, `DescribeStmt`, `CommentStmt`

```cpp
struct InsertStmt       { std::string table; std::vector<std::string> values; };
struct ColumnFormatStmt { std::string column; std::size_t width; };
struct DescribeStmt     { std::optional<std::string> table; };   // empty = list all tables
struct CommentStmt      {};                                       // carries no data
```

- `std::vector<std::string> values` would be `char **values; size_t valueCount;` in C. You'd free each string, then the array.
- `std::optional<std::string> table` would be `char *table` in C, with the convention that `NULL` means "no table". It works, but the rule exists only in a comment. In C++ the type says it.
- `CommentStmt {}` is an empty struct: the type itself is the information ("this line was a comment"). Plain C does not allow empty structs. You'd only have an enum value for it.

---

## 6. `Statement`: the key idea of the file

```cpp
using Statement = std::variant<SelectStmt, InsertStmt, UpdateStmt, DeleteStmt, ColumnFormatStmt, DescribeStmt, CommentStmt>;
```

(`DeleteStmt { std::string table; Condition where; }` and `UpdateStmt { std::string table; std::vector<Assignment> assignments; Condition where; }` were added on 2026-10-21, after this note was first written. `Assignment` is one `column = value` pair of the SET list.)

A `std::variant` holds **exactly one** of the listed types at a time, and it always knows which one. `using` simply gives that long type a short name.

**In C** this is a *tagged union*, written by hand:
```c
typedef enum { STMT_SELECT, STMT_INSERT, STMT_COLUMN_FORMAT, STMT_DESCRIBE, STMT_COMMENT } StatementKind;

typedef struct {
    StatementKind kind;        /* the tag: which member is valid */
    union {
        SelectStmt       select;
        InsertStmt       insert;
        ColumnFormatStmt columnFormat;
        DescribeStmt     describe;
    } as;
} Statement;

switch (st.kind) {
case STMT_SELECT: run_select(&st.as.select); break;
case STMT_INSERT: run_insert(&st.as.insert); break;
/* ... */
}
```

The C version has three weak points that `std::variant` fixes:

| Problem in C | What `std::variant` does |
| --- | --- |
| You set `kind = STMT_INSERT` but fill in `as.select` by mistake | Not possible: the variant stores the tag itself when you assign a value |
| You read `as.insert` while it holds a SELECT: garbage, no error | `std::get<InsertStmt>(st)` throws `std::bad_variant_access` |
| Union members that own memory need hand-written cleanup for each case | The variant destroys whichever member it holds |

`Session.cpp` dispatches like this:
```cpp
std::visit([this](const auto& stmt) { return run(stmt); }, statement);
```
`std::visit` calls the `run()` overload that matches the type currently inside. If you add a new statement type and forget its `run()`, **the program doesn't compile**. This really happened when `DeleteStmt` was added: the first build failed with `no matching function for call to 'Session::run(const DeleteStmt&)'` until `run(const DeleteStmt&)` was written. In C, a forgotten `case` is at most a warning (`-Wswitch`).

The 2023 equivalent was the `if (!Words[ct].compare("SELECT")) ... else if ...` chain in `SqlStatemen()`.

---

## 7. `parseStatement`

```cpp
/// Parses one statement. Throws SqlError on a syntax error.
Statement parseStatement(std::string_view sql);
```

- **Parameter `std::string_view`**: a pointer plus a length, read-only, **not a copy**. You can pass a `std::string`, a literal, or a slice of a larger string without copying anything.
- **Return value `Statement`**: the result is returned by value. Modern C++ moves it out cheaply, so no pointers or `malloc` are involved.
- **Errors** are reported with an exception (`SqlError`), not a return code.

**In C:**
```c
/* Returns 0 on success and fills *out; returns -1 on error and writes the message
   into errbuf. On success the caller must call statement_free(out). */
int parse_statement(const char *sql, Statement *out, char *errbuf, size_t errlen);
```
In C, the real result goes through an output pointer. Errors are an `int` that the caller can forget to check. The error text needs a buffer supplied by the caller. Ownership is a rule in a comment ("call `statement_free`"). And `const char *` must end with `'\0'`, so a slice of a larger string has to be copied first.

---

## 8. `splitStatements`

```cpp
/// Splits a script into statements: one per line, and `;` outside quotes also ends one.
std::vector<std::string> splitStatements(std::string_view script);
```

Reading it piece by piece:

- `///` says what the function does, including the quoting rule (`'a;b'` is not split).
- `std::string_view script` is the whole script text, read without copying.
- `std::vector<std::string>` is the result: a list of statements. The caller gets its own copy, and the memory is freed automatically when the vector goes out of scope.
- No error handling is needed, because splitting text can't fail.

Using it:
```cpp
for (const std::string& sql : splitStatements(script)) {
    results.push_back(execute(sql));
}   // all memory freed here, automatically
```

**In C:**
```c
/* Splits script into statements (one per line; ';' outside quotes also ends one).
   Returns a malloc'd array of malloc'd strings and stores its length in *count.
   Returns NULL if out of memory. Free the result with free_statements(). */
char **split_statements(const char *script, size_t *count);
void   free_statements(char **statements, size_t count);

/* using it */
size_t n;
char **list = split_statements(script, &n);
if (list == NULL) { /* handle out of memory */ }
for (size_t i = 0; i < n; i++) {
    execute(list[i]);
}
free_statements(list, n);   /* forget this = memory leak; call it twice = crash */
```

The C version needs four things the C++ version doesn't:
- a second function to free the result;
- an output parameter for the length;
- a `NULL` check;
- a long comment explaining who owns the memory.

**In the 2023 code** there was no split function. File mode read one line at a time. HTTP mode called `SqlGetOneWord(SqlCommand, '\n', i)` for line 0, 1, 2…, and each call split the **whole** request again just to return one line.

---

## The idea to take away

In the C style, the rules live in **comments and conventions**: "free this", "only valid if `hasWhere`", "`NULL` means none", "check the return code". In modern C++, the same rules live in the **types**: `std::optional`, `std::vector`, `std::variant`, `std::string`. The compiler enforces them, and destructors do the cleanup.
