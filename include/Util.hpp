/// @file Util.hpp
/// @brief The SqlError exception and small string helpers used everywhere.

#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace tinysql {

/// @brief A problem with one statement: bad syntax, unknown table, value too long...
///
/// Session::execute() catches it, reports the message, and carries on with the next statement.
class SqlError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;  ///< Same constructors: `SqlError("message")`.
};

/// @brief Removes trailing spaces, tabs and line ends.
/// @param text The text to trim; it is not copied.
/// @return A view of `text` without its trailing whitespace.
std::string_view trimRight(std::string_view text);

/// @brief Removes leading and trailing spaces, tabs and line ends.
/// @param text The text to trim; it is not copied.
/// @return A view of `text` without whitespace at either end.
std::string_view trim(std::string_view text);

/// @brief Converts ASCII letters to upper case.
/// @param text The text to convert.
/// @return An upper-case copy of `text`.
std::string toUpper(std::string_view text);

/// @brief Compares two strings, ignoring the case of ASCII letters.
/// @param a First string.
/// @param b Second string.
/// @return True if `a` and `b` are equal apart from upper/lower case.
bool equalsIgnoreCase(std::string_view a, std::string_view b);

/// @brief Matches text against an SQL `LIKE` pattern, ignoring the case of ASCII letters.
///
/// In the pattern, `%` stands for any text (also none) and `_` for exactly one character;
/// every other character must match itself. For example `%fran%` matches "Francesco" and "FRANCO",
/// and `_uigi` matches "Luigi". Characters are counted in bytes, so `_` does not match an accented
/// letter such as `ò`, which takes 2 bytes in UTF-8; use `%` there.
/// @param text The text to test.
/// @param pattern The pattern.
/// @return True if the whole text matches the whole pattern.
bool matchesLike(std::string_view text, std::string_view pattern);

/// @brief Parses a whole string as a base-10 integer.
/// @param text The text to parse; surrounding whitespace and a leading `+` are allowed.
/// @return The number, or `std::nullopt` if `text` is not exactly one integer.
std::optional<long long> parseInteger(std::string_view text);

}  // namespace tinysql
