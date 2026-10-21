#include "Util.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>

namespace tinysql {

namespace {
bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
char upper(unsigned char c) { return static_cast<char>(std::toupper(c)); }
}  // namespace

std::string_view trimRight(std::string_view text)
{
    while (!text.empty() && isSpace(text.back())) {
        text.remove_suffix(1);
    }
    return text;
}

std::string_view trim(std::string_view text)
{
    while (!text.empty() && isSpace(text.front())) {
        text.remove_prefix(1);
    }
    return trimRight(text);
}

std::string toUpper(std::string_view text)
{
    std::string result(text);
    std::ranges::transform(result, result.begin(), [](unsigned char c) { return upper(c); });
    return result;
}

bool equalsIgnoreCase(std::string_view a, std::string_view b)
{
    return std::ranges::equal(a, b, [](unsigned char x, unsigned char y) { return upper(x) == upper(y); });
}

std::optional<long long> parseInteger(std::string_view text)
{
    text = trim(text);
    if (text.starts_with('+')) {
        text.remove_prefix(1);
    }
    if (text.empty()) {
        return std::nullopt;
    }
    long long value = 0;
    const char* end = text.data() + text.size();
    auto [ptr, ec] = std::from_chars(text.data(), end, value);
    if (ec != std::errc{} || ptr != end) {
        return std::nullopt;
    }
    return value;
}

}  // namespace tinysql
