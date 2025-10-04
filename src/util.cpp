#include "util.h"

std::vector<std::string> split(const std::string &s, std::string_view delimiter)
{
    std::vector<std::string> tokens{};
    std::size_t pos{};

    while (true)
    {
        const auto delimiter_index = s.find(delimiter, pos);
        if (delimiter_index == std::string::npos)
            break;

        std::string token = s.substr(pos, delimiter_index - pos);

        tokens.emplace_back(token);

        pos = delimiter_index + delimiter.length();
    }

    tokens.emplace_back(s.substr(pos));

    return tokens;
}

std::vector<std::string_view> split_sv(std::string_view s, std::string_view delimiter)
{
    std::vector<std::string_view> tokens{};
    std::size_t pos{};

    while (true)
    {
        const auto delimiter_index = s.find(delimiter, pos);
        if (delimiter_index == std::string::npos)
            break;

        std::string_view token = s.substr(pos, delimiter_index - pos);

        tokens.emplace_back(token);

        pos = delimiter_index + delimiter.length();
    }

    tokens.emplace_back(s.substr(pos));

    return tokens;
}

constexpr std::string pretty_space_add(std::string_view s)
{
    using namespace std::literals;

    const auto v = split_sv(s, "\n"sv) | std::views::join_with("\n    "sv);
    return std::string(v.begin(), v.end());
}