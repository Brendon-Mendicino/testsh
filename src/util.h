#ifndef TESTSH_UTIL_H
#define TESTSH_UTIL_H

#include <format>
#include <string>
#include <string_view>
#include <vector>
#include <ranges>
#include <iomanip>
#include <memory>
#include <optional>
#include <cstdlib>
#include <cxxabi.h>
#include <cassert>

// helper function for assertions with message
#define assertm(exp, msg)  assert((void(msg), exp))

// helper type for the visitor
template <class... Ts>
struct overloads : Ts...
{
    using Ts::operator()...;
};

// Helper type for an optional pointer
template <typename T>
using optional_ptr = std::optional<std::unique_ptr<T>>;

template <typename T, typename... Args>
constexpr inline optional_ptr<T> make_optptr(Args &&...args)
{
    return std::optional<std::unique_ptr<T>>{std::make_unique<T>(std::forward<Args>(args)...)};
}

// helper for assigning debug format to a std::formatter
struct debug_spec
{
    bool debug = false;
    bool pretty = false;
    int spaces = 4;

    constexpr void set_debug_format()
    {
        this->debug = true;
    }

    constexpr auto parse(auto &ctx)
    {
        auto it = ctx.begin();
        const auto end = ctx.end();

        if (*it == '#')
        {
            this->pretty = true;
            ++it;
        }

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

    // Using this method to pretty print (basically add spaces)
    // to the formatted objects.
    template <typename Formatted>
    auto p_format(const Formatted &p, auto &ctx) const
    {
        std::formatter<Formatted> fmt;
        fmt.debug = true;
        fmt.pretty = true;
        fmt.spaces = this->spaces + 4;

        return fmt.format(p, ctx);
    }
};

std::vector<std::string> split(const std::string &s, std::string_view delimiter);

std::vector<std::string_view> split_sv(std::string_view s, std::string_view delimiter);

template <typename T>
concept HasFormatter =
    requires(T value, std::format_context &ctx, std::formatter<T> fmt) {
        { fmt.format(value, ctx) } -> std::same_as<typename std::format_context::iterator>;
    };

template <typename T>
inline std::string typeid_name()
{
    int status;
    char *realname = abi::__cxa_demangle(typeid(T).name(), nullptr, nullptr, &status);

    if (realname == nullptr || status != 0)
        throw std::runtime_error(std::format("typeid.name retrieval failed! status={}", status));

    std::string name{realname};

    std::free(realname);

    return name;
}

template <typename T, typename CharT>
struct std::formatter<std::vector<T>, CharT>
{
    // Reuse existing formatter for elements
    std::formatter<T, CharT> elem_fmt;

    // parse optional format specifiers (forward to element formatter)
    constexpr auto parse(std::basic_format_parse_context<CharT> &ctx)
    {
        return elem_fmt.parse(ctx);
    }

    template <typename FormatContext>
    typename FormatContext::iterator
    format(const std::vector<T> &vec, FormatContext &ctx) const
    {
        auto out = ctx.out();
        *out++ = '[';

        for (size_t i = 0; i < vec.size(); ++i)
        {
            out = elem_fmt.format(vec[i], ctx);
            if (i + 1 < vec.size())
            {
                *out++ = ',';
                *out++ = ' ';
            }
        }

        *out++ = ']';
        return out;
    }
};

// template <typename T, typename CharT>
// struct std::formatter<std::vector<T>, CharT> : debug_spec 
// {
//     template <typename FormatContext>
//     typename FormatContext::iterator
//     format(const std::vector<T> &vec, FormatContext &ctx) const
//     {
//         // Reuse existing formatter for elements
//         std::formatter<T, CharT> elem_fmt{*this};
//         const std::string sspaces(this->spaces, ' ');

//         auto out = ctx.out();
//         *out++ = '[';

//         for (size_t i = 0; i < vec.size(); ++i)
//         {
//             if (this->pretty)
//                 out = std::format_to(ctx.out(), "\n{}", sspaces);

//             out = elem_fmt.format(vec[i], ctx);
//             if (i + 1 < vec.size())
//             {
//                 *out++ = ',';
//                 *out++ = ' ';
//             }
//         }

//         *out++ = ']';
//         return out;
//     }
// };

template <typename T, typename CharT>
struct std::formatter<std::unique_ptr<T>, CharT> : debug_spec
{

    template <typename FormatContext>
    typename FormatContext::iterator
    format(const std::unique_ptr<T> &ptr, FormatContext &ctx) const
    {
        // Reuse existing formatter for elements
        std::formatter<T, CharT> elem_fmt{*this};

        if (!ptr)
        {
            return std::format_to(ctx.out(), "unique_ptr(null)");
        }

        const T &elem = *ptr;

        std::format_to(ctx.out(), "unique_ptr(");
        elem_fmt.format(elem, ctx);
        return std::format_to(ctx.out(), ")");
    }
};

// template specialization for a std::variant
// whose types already have a formatter.
template <typename... Types>
struct std::formatter<std::variant<Types...>> : debug_spec
{
    using VarType = std::variant<Types...>;

    auto format(const VarType &v, auto &ctx) const
    {
        return std::visit(
            [&]<HasFormatter T>(const T &p)
            {
                const std::formatter<T> fmt{*this};
                return fmt.format(p, ctx);
            },
            v);
    }
};

// template specialization for a std::optional
template <typename T, typename CharT>
struct std::formatter<std::optional<T>, CharT> : debug_spec
{
    template <typename FormatContext>
    typename FormatContext::iterator
    format(const std::optional<T> &ptr, FormatContext &ctx) const
    {
        // Reuse existing formatter for elements
        std::formatter<T, CharT> elem_fmt{*this};

        if (ptr.has_value())
        {
            std::format_to(ctx.out(), "Some(");
            elem_fmt.format(*ptr, ctx);
            return std::format_to(ctx.out(), ")");
        }
        else
        {
            return std::format_to(ctx.out(), "None");
        }
    }
};

#endif // TESTSH_UTIL_H