/// @file Parser.cpp
/// @brief The tokenizer and recursive-descent parser for the SQL dialect.
///
/// The grammar:
///
///     SELECT <item>[,<item>...] FROM <table> [WHERE <col> <op> <value>]
///     INSERT INTO <table> VALUES (<value>[,<value>...])
///     UPDATE <table> SET <col> = <value>[,<col> = <value>...] WHERE <col> <op> <value>
///     DELETE FROM <table> WHERE <col> <op> <value>
///     CREATE TABLE <table> (<col> CHAR(<width>)|NUMBER(<width>)[,...])
///     COLUMN <col> FORMAT A<width>
///     DESCRIBE [<table>]
///     REM <comment>    -- <comment>
///
/// `<op>` is one of `=` `!=` `<>` `<` `<=` `>` `>=` or `LIKE` (with `%` and `_` wildcards).
/// Keywords are case-insensitive. A trailing `;` or `@` is ignored.

#include <format>
#include <string_view>
#include <utility>

#include "Statement.hpp"
#include "Util.hpp"

namespace tinysql {

namespace {

/// @brief The kinds of token the tokenizer produces.
enum class TokenKind {
    Word,    ///< A keyword, name or bare value, e.g. `SELECT`, `EMPLOY`, `Torino`, `42`.
    String,  ///< A `'quoted string'`, without its quotes.
    Symbol,  ///< Punctuation or an operator: `( ) , * ; @ = != <> < <= > >=`.
    End,     ///< The end of the statement.
};

/// @brief One token of a statement.
struct Token {
    TokenKind kind;    ///< What kind of token this is.
    std::string text;  ///< Its text.
};

/// @brief The characters that end a word.
constexpr std::string_view kDelimiters = " \t\r\n(),*;@=<>!'";

/// @brief Splits a statement into tokens.
/// @param sql The statement.
/// @return The tokens, always ending with one TokenKind::End.
/// @throws SqlError On an unterminated quoted string or a lone `!`.
std::vector<Token> tokenize(std::string_view sql)
{
    std::vector<Token> tokens;
    std::size_t i = 0;
    while (i < sql.size()) {
        const char c = sql[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            ++i;
        } else if (c == '\'') {
            // 'quoted string', with '' standing for one quote
            std::string text;
            ++i;
            while (true) {
                if (i >= sql.size()) {
                    throw SqlError("unterminated quoted string");
                }
                if (sql[i] == '\'') {
                    if (i + 1 < sql.size() && sql[i + 1] == '\'') {
                        text += '\'';
                        i += 2;
                        continue;
                    }
                    ++i;
                    break;
                }
                text += sql[i++];
            }
            tokens.push_back({TokenKind::String, std::move(text)});
        } else if (std::string_view("(),*;@").find(c) != std::string_view::npos) {
            tokens.push_back({TokenKind::Symbol, std::string(1, c)});
            ++i;
        } else if (std::string_view("=<>!").find(c) != std::string_view::npos) {
            std::string_view two = sql.substr(i, 2);
            if (two == "<=" || two == ">=" || two == "!=" || two == "<>") {
                tokens.push_back({TokenKind::Symbol, std::string(two)});
                i += 2;
            } else if (c == '!') {
                throw SqlError("unexpected '!'");
            } else {
                tokens.push_back({TokenKind::Symbol, std::string(1, c)});
                ++i;
            }
        } else {
            const std::size_t end = sql.find_first_of(kDelimiters, i);
            const std::size_t stop = end == std::string_view::npos ? sql.size() : end;
            tokens.push_back({TokenKind::Word, std::string(sql.substr(i, stop - i))});
            i = stop;
        }
    }
    tokens.push_back({TokenKind::End, ""});
    return tokens;
}

/// @brief Describes a token for an error message.
/// @param token The token.
/// @return E.g. `"FROM"`, ``'text'``, or "end of statement".
std::string describe(const Token& token)
{
    switch (token.kind) {
    case TokenKind::End:
        return "end of statement";
    case TokenKind::String:
        return std::format("'{}'", token.text);
    default:
        return std::format("\"{}\"", token.text);
    }
}

/// @brief Tells whether a symbol is a comparison operator.
/// @param op The symbol.
/// @return True for `=` `!=` `<>` `<` `<=` `>` `>=`.
bool isComparison(std::string_view op)
{
    return op == "=" || op == "!=" || op == "<>" || op == "<" || op == "<=" || op == ">" || op == ">=";
}

/// @brief A recursive-descent parser: one method per statement type, reading the tokens left to right.
class Parser {
public:
    /// @brief Prepares to parse a statement.
    /// @param tokens The statement's tokens, from tokenize().
    explicit Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

    /// @brief Parses the whole statement, choosing the method from its first word.
    /// @return The parsed statement.
    /// @throws SqlError On an unknown command or a syntax error.
    Statement parse()
    {
        const std::string keyword = toUpper(peek().text);
        if (peek().kind != TokenKind::Word) {
            fail("a command");
        }
        if (keyword == "SELECT") return parseSelect();
        if (keyword == "INSERT") return parseInsert();
        if (keyword == "UPDATE") return parseUpdate();
        if (keyword == "DELETE") return parseDelete();
        if (keyword == "CREATE") return parseCreateTable();
        if (keyword == "COLUMN") return parseColumn();
        if (keyword == "DESCRIBE") return parseDescribe();
        throw SqlError(std::format("unknown command \"{}\"", peek().text));
    }

private:
    /// @brief The tokens, ending with TokenKind::End.
    std::vector<Token> tokens_;
    /// @brief The position of the next token to read.
    std::size_t pos_ = 0;

    /// @brief Looks at the next token without consuming it.
    /// @return The next token.
    const Token& peek() const { return tokens_[pos_]; }
    /// @brief Consumes the next token (the End token is never consumed).
    /// @return The token.
    Token next() { return peek().kind == TokenKind::End ? peek() : tokens_[pos_++]; }

    /// @brief Reports a syntax error at the current token.
    /// @param expected What should have come, e.g. "a table name".
    /// @throws SqlError Always: "expected ... but found ...".
    [[noreturn]] void fail(std::string_view expected) const
    {
        throw SqlError(std::format("expected {} but found {}", expected, describe(peek())));
    }

    /// @brief Consumes the next token if it is a given keyword.
    /// @param keyword The keyword (case-insensitive).
    /// @return True if it was there and was consumed.
    bool acceptKeyword(std::string_view keyword)
    {
        if (peek().kind == TokenKind::Word && equalsIgnoreCase(peek().text, keyword)) {
            ++pos_;
            return true;
        }
        return false;
    }

    /// @brief Consumes a keyword that must come next.
    /// @param keyword The keyword (case-insensitive).
    /// @throws SqlError If the next token is something else.
    void expectKeyword(std::string_view keyword)
    {
        if (!acceptKeyword(keyword)) {
            fail(keyword);
        }
    }

    /// @brief Consumes the next token if it is a given symbol.
    /// @param symbol The symbol, e.g. ",".
    /// @return True if it was there and was consumed.
    bool acceptSymbol(std::string_view symbol)
    {
        if (peek().kind == TokenKind::Symbol && peek().text == symbol) {
            ++pos_;
            return true;
        }
        return false;
    }

    /// @brief Consumes a symbol that must come next.
    /// @param symbol The symbol, e.g. "(".
    /// @throws SqlError If the next token is something else.
    void expectSymbol(std::string_view symbol)
    {
        if (!acceptSymbol(symbol)) {
            fail(std::format("'{}'", symbol));
        }
    }

    /// @brief Consumes a word (a name) that must come next.
    /// @param what What the word is, for the error message, e.g. "a column name".
    /// @return The word.
    /// @throws SqlError If the next token is not a word.
    std::string expectWord(std::string_view what)
    {
        if (peek().kind != TokenKind::Word) {
            fail(what);
        }
        return next().text;
    }

    /// @brief Consumes a value that must come next: a word or a quoted string.
    /// @return The value's text.
    /// @throws SqlError If the next token is neither.
    std::string expectValue()
    {
        if (peek().kind != TokenKind::Word && peek().kind != TokenKind::String) {
            fail("a value");
        }
        return next().text;
    }

    /// @brief Checks that the statement ends here, after any trailing `;` or `@`.
    /// @throws SqlError If more tokens follow.
    void expectEnd()
    {
        while (acceptSymbol(";") || acceptSymbol("@")) {
        }
        if (peek().kind != TokenKind::End) {
            fail("end of statement");
        }
    }

    /// @brief Parses `<column> <op> <value>`, after the WHERE keyword; `<op>` is a comparison or `LIKE`.
    /// @return The condition.
    /// @throws SqlError On a syntax error.
    Condition parseCondition()
    {
        Condition condition;
        condition.column = expectWord("a column name");
        if (acceptKeyword("LIKE")) {
            condition.op = "LIKE";
        } else {
            if (peek().kind != TokenKind::Symbol || !isComparison(peek().text)) {
                fail("a comparison (= != < <= > >= or LIKE)");
            }
            condition.op = next().text;
        }
        condition.value = expectValue();
        return condition;
    }

    /// @brief Parses `SELECT <items> FROM <table> [WHERE ...]`.
    /// @return The SelectStmt.
    /// @throws SqlError On a syntax error.
    SelectStmt parseSelect()
    {
        expectKeyword("SELECT");
        SelectStmt stmt;
        do {
            if (acceptSymbol("*")) {
                stmt.items.push_back({SelectItem::Kind::Star, "*"});
            } else if (peek().kind == TokenKind::String) {
                stmt.items.push_back({SelectItem::Kind::Literal, next().text});
            } else {
                stmt.items.push_back({SelectItem::Kind::Column, expectWord("a column name")});
            }
        } while (acceptSymbol(","));

        expectKeyword("FROM");
        stmt.table = expectWord("a table name");

        if (acceptKeyword("WHERE")) {
            stmt.where = parseCondition();
        }

        expectEnd();
        return stmt;
    }

    /// @brief Parses `INSERT INTO <table> VALUES (...)`. A value is a quoted string, or one or more bare words joined by spaces.
    /// @return The InsertStmt.
    /// @throws SqlError On a syntax error.
    InsertStmt parseInsert()
    {
        expectKeyword("INSERT");
        expectKeyword("INTO");
        InsertStmt stmt;
        stmt.table = expectWord("a table name");
        expectKeyword("VALUES");
        expectSymbol("(");
        do {
            // A value is a 'quoted string', or one or more bare words joined by single spaces.
            if (peek().kind == TokenKind::String) {
                stmt.values.push_back(next().text);
                continue;
            }
            std::string value;
            while (peek().kind == TokenKind::Word) {
                if (!value.empty()) {
                    value += ' ';
                }
                value += next().text;
            }
            stmt.values.push_back(std::move(value));
        } while (acceptSymbol(","));
        expectSymbol(")");
        expectEnd();
        return stmt;
    }

    /// @brief Parses `UPDATE <table> SET <col> = <value>, ... WHERE ...`.
    /// @return The UpdateStmt.
    /// @throws SqlError On a syntax error.
    UpdateStmt parseUpdate()
    {
        expectKeyword("UPDATE");
        UpdateStmt stmt;
        stmt.table = expectWord("a table name");
        expectKeyword("SET");
        do {
            Assignment assignment;
            assignment.column = expectWord("a column name");
            expectSymbol("=");
            assignment.value = expectValue();
            if (peek().kind == TokenKind::Word && !equalsIgnoreCase(peek().text, "WHERE")) {
                throw SqlError(std::format("put a value with spaces in quotes, e.g. {} = '{} {}'",
                                           assignment.column, assignment.value, peek().text));
            }
            stmt.assignments.push_back(std::move(assignment));
        } while (acceptSymbol(","));
        if (!acceptKeyword("WHERE")) {
            fail("WHERE (UPDATE always needs a condition, e.g. WHERE ID = 3)");
        }
        stmt.where = parseCondition();
        expectEnd();
        return stmt;
    }

    /// @brief Parses `DELETE FROM <table> WHERE ...`.
    /// @return The DeleteStmt.
    /// @throws SqlError On a syntax error.
    DeleteStmt parseDelete()
    {
        expectKeyword("DELETE");
        expectKeyword("FROM");
        DeleteStmt stmt;
        stmt.table = expectWord("a table name");
        if (!acceptKeyword("WHERE")) {
            fail("WHERE (DELETE always needs a condition, e.g. WHERE ID = 3)");
        }
        stmt.where = parseCondition();
        expectEnd();
        return stmt;
    }

    /// @brief Parses `CREATE TABLE <table> (<col> <type>(<width>), ...)`, where `<type>` is CHAR or NUMBER.
    /// @return The CreateTableStmt.
    /// @throws SqlError On a syntax error, an unknown type, or a width that is not a positive integer.
    CreateTableStmt parseCreateTable()
    {
        expectKeyword("CREATE");
        expectKeyword("TABLE");
        CreateTableStmt stmt;
        stmt.table = expectWord("a table name");
        expectSymbol("(");
        do {
            ColumnDefinition column;
            column.name = expectWord("a column name");
            const std::string type = toUpper(expectWord("a type (CHAR or NUMBER)"));
            if (type == "CHAR") {
                column.type = "S1";
            } else if (type == "NUMBER") {
                column.type = "N1";
            } else {
                throw SqlError(std::format("unknown type \"{}\" for column {}: use CHAR(<width>) or NUMBER(<width>)",
                                           type, column.name));
            }
            expectSymbol("(");
            const std::string width = expectWord("a width");
            auto value = parseInteger(width);
            if (!value || *value < 1) {
                throw SqlError(std::format("invalid width \"{}\" for column {}: expected a positive number",
                                           width, column.name));
            }
            column.width = static_cast<std::size_t>(*value);
            expectSymbol(")");
            stmt.columns.push_back(std::move(column));
        } while (acceptSymbol(","));
        expectSymbol(")");
        expectEnd();
        return stmt;
    }

    /// @brief Parses `COLUMN <col> FORMAT A<width>`.
    /// @return The ColumnFormatStmt.
    /// @throws SqlError On a syntax error.
    ColumnFormatStmt parseColumn()
    {
        expectKeyword("COLUMN");
        ColumnFormatStmt stmt;
        stmt.column = expectWord("a column name");
        expectKeyword("FORMAT");
        const std::string format = expectWord("a format such as A16");
        auto width = parseInteger(std::string_view(format).substr(1));
        if (format.size() < 2 || (format[0] != 'A' && format[0] != 'a') || !width || *width < 1) {
            throw SqlError(std::format("invalid format \"{}\", expected A<width> such as A16", format));
        }
        stmt.width = static_cast<std::size_t>(*width);
        expectEnd();
        return stmt;
    }

    /// @brief Parses `DESCRIBE [<table>]`.
    /// @return The DescribeStmt.
    /// @throws SqlError On a syntax error.
    DescribeStmt parseDescribe()
    {
        expectKeyword("DESCRIBE");
        DescribeStmt stmt;
        if (peek().kind == TokenKind::Word) {
            stmt.table = next().text;
        }
        expectEnd();
        return stmt;
    }
};

/// @brief Tells whether a line is a comment or empty.
/// @param sql The line.
/// @return True for an empty line, `-- ...`, or `REM ...`.
bool isComment(std::string_view sql)
{
    sql = trim(sql);
    if (sql.empty() || sql.starts_with("--")) {
        return true;
    }
    const std::string_view first = sql.substr(0, sql.find_first_of(" \t"));
    return equalsIgnoreCase(first, "REM");
}

}  // namespace

Statement parseStatement(std::string_view sql)
{
    if (isComment(sql)) {
        return CommentStmt{};
    }
    return Parser(tokenize(sql)).parse();
}

std::vector<std::string> splitStatements(std::string_view script)
{
    std::vector<std::string> statements;
    std::size_t lineStart = 0;
    while (lineStart <= script.size()) {
        std::size_t lineEnd = script.find('\n', lineStart);
        if (lineEnd == std::string_view::npos) {
            lineEnd = script.size();
        }
        const std::string_view line = script.substr(lineStart, lineEnd - lineStart);
        lineStart = lineEnd + 1;

        if (isComment(line)) {
            continue;
        }
        std::string current;
        bool inQuotes = false;
        for (char c : line) {
            if (c == '\'') {
                inQuotes = !inQuotes;
            }
            if (c == ';' && !inQuotes) {
                if (!trim(current).empty()) {
                    statements.emplace_back(trim(current));
                }
                current.clear();
            } else {
                current += c;
            }
        }
        if (!trim(current).empty()) {
            statements.emplace_back(trim(current));
        }
    }
    return statements;
}

}  // namespace tinysql
