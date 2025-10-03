#ifndef TESTSH_UTIL_H
#define TESTSH_UTIL_H

#include <format>

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

#endif // TESTSH_UTIL_H