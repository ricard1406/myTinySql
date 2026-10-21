// Tokenizer and recursive-descent parser for the tiny SQL dialect.
//
//   SELECT <item>[,<item>...] FROM <table> [WHERE <col> <op> <value>]
//          [ANDROID-LIST <from> <to> | ANDROID-DISTINCT]
//   INSERT INTO <table> VALUES (<value>[,<value>...])
//   UPDATE <table> SET <col> = <value>[,<col> = <value>...] WHERE <col> <op> <value>
//   DELETE FROM <table> WHERE <col> <op> <value>
//   COLUMN <col> FORMAT A<width>
//   DESCRIBE [<table>]
//   REM <comment>    -- <comment>
//
// Keywords are case-insensitive. A trailing ';' or '@' is ignored.

#include <format>
#include <string_view>
#include <utility>

#include "Statement.hpp"
#include "Util.hpp"

namespace tinysql {

namespace {

enum class TokenKind { Word, String, Symbol, End };

struct Token {
    TokenKind kind;
    std::string text;
};

constexpr std::string_view kDelimiters = " \t\r\n(),*;@=<>!'";

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

bool isComparison(std::string_view op)
{
    return op == "=" || op == "!=" || op == "<>" || op == "<" || op == "<=" || op == ">" || op == ">=";
}

class Parser {
public:
    explicit Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

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
        if (keyword == "COLUMN") return parseColumn();
        if (keyword == "DESCRIBE") return parseDescribe();
        throw SqlError(std::format("unknown command \"{}\"", peek().text));
    }

private:
    std::vector<Token> tokens_;
    std::size_t pos_ = 0;

    const Token& peek() const { return tokens_[pos_]; }
    Token next() { return peek().kind == TokenKind::End ? peek() : tokens_[pos_++]; }

    [[noreturn]] void fail(std::string_view expected) const
    {
        throw SqlError(std::format("expected {} but found {}", expected, describe(peek())));
    }

    bool acceptKeyword(std::string_view keyword)
    {
        if (peek().kind == TokenKind::Word && equalsIgnoreCase(peek().text, keyword)) {
            ++pos_;
            return true;
        }
        return false;
    }

    void expectKeyword(std::string_view keyword)
    {
        if (!acceptKeyword(keyword)) {
            fail(keyword);
        }
    }

    bool acceptSymbol(std::string_view symbol)
    {
        if (peek().kind == TokenKind::Symbol && peek().text == symbol) {
            ++pos_;
            return true;
        }
        return false;
    }

    void expectSymbol(std::string_view symbol)
    {
        if (!acceptSymbol(symbol)) {
            fail(std::format("'{}'", symbol));
        }
    }

    std::string expectWord(std::string_view what)
    {
        if (peek().kind != TokenKind::Word) {
            fail(what);
        }
        return next().text;
    }

    std::string expectValue()
    {
        if (peek().kind != TokenKind::Word && peek().kind != TokenKind::String) {
            fail("a value");
        }
        return next().text;
    }

    std::size_t expectPositive(std::string_view what)
    {
        auto value = parseInteger(peek().kind == TokenKind::Word ? peek().text : "");
        if (!value || *value < 1) {
            fail(what);
        }
        ++pos_;
        return static_cast<std::size_t>(*value);
    }

    void expectEnd()
    {
        while (acceptSymbol(";") || acceptSymbol("@")) {
        }
        if (peek().kind != TokenKind::End) {
            fail("end of statement");
        }
    }

    /// <column> <op> <value>, after the WHERE keyword.
    Condition parseCondition()
    {
        Condition condition;
        condition.column = expectWord("a column name");
        if (peek().kind != TokenKind::Symbol || !isComparison(peek().text)) {
            fail("a comparison (= != < <= > >=)");
        }
        condition.op = next().text;
        condition.value = expectValue();
        return condition;
    }

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

        if (acceptKeyword("ANDROID-LIST")) {
            ColumnRange range;
            range.from = expectPositive("the first column number");
            range.to = expectPositive("the last column number");
            if (range.to < range.from) {
                throw SqlError("ANDROID-LIST: the last column comes before the first");
            }
            stmt.androidList = range;
        } else if (acceptKeyword("ANDROID-DISTINCT")) {
            stmt.androidDistinct = true;
        }

        expectEnd();
        return stmt;
    }

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
