#ifndef TESTSH_UTIL_H
#define TESTSH_UTIL_H

#include <format>
#include <string>
#include <string_view>
#include <vector>

struct debug_spec
{
    bool debug = false;

    constexpr void set_debug_format()
    {
        this->debug = true;
    }

    constexpr auto parse(auto &ctx)
    {
        auto it = ctx.begin();
        const auto end = ctx.end();

        if (*it == '?')
        {
            this->set_debug_format();
            ++it;
        }
        else
        {
            throw std::format_error("Invalid format args. Specification must be debug {:?}");
        }

        if (it != end && *it != '}')
            throw std::format_error("Invalid format args. Specification must be debug {:?}");

        return it;
    }
};

std::vector<std::string> split(const std::string &s, std::string_view delimiter);

std::vector<std::string_view> split_sv(std::string_view s, std::string_view delimiter);

template <typename T, typename CharT>
struct std::formatter<std::vector<T>, CharT> {
    // Reuse existing formatter for elements
    std::formatter<T, CharT> elem_fmt;

    // parse optional format specifiers (forward to element formatter)
    constexpr auto parse(std::basic_format_parse_context<CharT>& ctx) {
        return elem_fmt.parse(ctx);
    }

    template <typename FormatContext>
    auto format(const std::vector<T>& vec, FormatContext& ctx) const {
        auto out = ctx.out();
        *out++ = '[';

        for (size_t i = 0; i < vec.size(); ++i) {
            out = elem_fmt.format(vec[i], ctx);
            if (i + 1 < vec.size()) {
                *out++ = ',';
                *out++ = ' ';
            }
        }

        *out++ = ']';
        return out;
    }
};


#endif // TESTSH_UTIL_H