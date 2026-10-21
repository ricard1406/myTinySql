#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace tinysql {

/// A problem with one statement (bad syntax, unknown table, value too long...).
/// The session reports it and carries on with the next statement.
class SqlError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

std::string_view trimRight(std::string_view text);
std::string_view trim(std::string_view text);
std::string toUpper(std::string_view text);
bool equalsIgnoreCase(std::string_view a, std::string_view b);

/// Parses a whole string as a base-10 integer; std::nullopt if it is not one.
std::optional<long long> parseInteger(std::string_view text);

}  // namespace tinysql
