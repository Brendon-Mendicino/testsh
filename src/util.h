#ifndef TESTSH_UTIL_H
#define TESTSH_UTIL_H

#include <cassert>
#include <concepts>
#include <cstdlib>
#include <cxxabi.h>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unistd.h>
#include <variant>
#include <vector>

// ----------------------------------
// DEFINES
// ----------------------------------

// helper function for assertions with message
#define assertm(exp, msg) assert((void(msg), exp))

// ----------------------------------
// HELPERS
// ----------------------------------

// helper type for the visitor
template <class... Ts> struct overloads : Ts... {
    using Ts::operator()...;
};

// Helper type for an optional pointer
template <typename T> using optional_ptr = std::optional<std::unique_ptr<T>>;

template <typename T, typename... Args>
constexpr inline optional_ptr<T> make_optptr(Args &&...args) {
    return std::optional<std::unique_ptr<T>>{
        std::make_unique<T>(std::forward<Args>(args)...)};
}

std::vector<std::string> split(const std::string &s,
                               std::string_view delimiter);

std::vector<std::string_view> split_sv(std::string_view s,
                                       std::string_view delimiter);

template <typename T>
concept HasFormatter =
    requires(T value, std::format_context &ctx, std::formatter<T> fmt) {
        {
            fmt.format(value, ctx)
        } -> std::same_as<typename std::format_context::iterator>;
    };

struct debug_spec;

template <typename T, typename CharT = char>
concept HasDebugFormatter =
    std::derived_from<std::formatter<T, CharT>, debug_spec>;

struct Token;

template <typename T>
concept IsTokenizer = std::copyable<T> && std::movable<T> &&
                      requires(const T t) {
                          { t.next_is_eof() } -> std::same_as<bool>;
                          { t.peek() } -> std::same_as<std::optional<Token>>;
                      } &&

                      requires(T t2) {
                          {
                              t2.next_token()
                          } -> std::same_as<std::optional<Token>>;
                      };

template <typename T> inline std::string typeid_name() {
    int status;
    char *realname =
        abi::__cxa_demangle(typeid(T).name(), nullptr, nullptr, &status);

    if (realname == nullptr || status != 0)
        throw std::runtime_error(
            std::format("typeid.name retrieval failed! status={}", status));

    std::string name{realname};

    std::free(realname);

    return name;
}

// ----------------------------------
// CLASSES
// ----------------------------------

// ----------------------------------
// FORMATTING
// ----------------------------------

// helper for assigning debug format to a std::formatter
struct debug_spec {
    bool debug = false;
    bool pretty = false;
    int spaces = 4;

    constexpr void set_debug_format() { this->debug = true; }

    constexpr auto parse(auto &ctx) {
        auto it = ctx.begin();
        const auto end = ctx.end();

        if (*it == '#') {
            this->pretty = true;
            ++it;
        }

        if (*it == '?') {
            this->set_debug_format();
            ++it;
        } else {
            throw std::format_error(
                "Invalid format args. Specification must be debug {:?}");
        }

        if (it != end && *it != '}')
            throw std::format_error(
                "Invalid format args. Specification must be debug {:?}");

        return it;
    }

    template <typename T, typename CharT = char>
    inline auto iformat(const T &t, auto &ctx) const {
        std::formatter<T, CharT> fmt{};

        return fmt.format(t, ctx);
    }

    template <typename T, typename CharT = char>
        requires HasDebugFormatter<T, CharT>
    inline auto iformat(const T &t, auto &ctx) const {
        std::formatter<T, CharT> fmt{*this};

        return fmt.format(t, ctx);
    }

    // Using this method to pretty print (basically add spaces)
    // to the formatted objects.
    template <typename Formatted>
    inline auto p_format(const Formatted &p, auto &ctx) const {
        std::formatter<Formatted> fmt{*this};
        if (this->pretty) {
            fmt.spaces += 4;
        }

        return fmt.format(p, ctx);
    }

    // Print the start of the class
    template <typename T> auto start(auto &ctx) const {
        return std::format_to(ctx.out(), "{}(", typeid_name<T>());
    }

    // Print a field that is not backed by debug_spec
    template <typename Field>
    auto field(std::string_view name, const Field &p, auto &ctx) const {
        if (this->pretty) {
            std::format_to(ctx.out(), "\n{}", std::string(this->spaces, ' '));
        }

        std::format_to(ctx.out(), "{}={}", name, p);

        if (this->pretty) {
            std::format_to(ctx.out(), ",");
        } else {
            // WARNING: also the last item gets printed with
            std::format_to(ctx.out(), ", ");
        }

        return ctx.out();
    }

    // Print a field that is backed by debug_spec
    template <typename Field>
        requires HasDebugFormatter<Field>
    auto field(std::string_view name, const Field &p, auto &ctx) const {
        if (this->pretty) {
            std::format_to(ctx.out(), "\n{}", std::string(this->spaces, ' '));
        }

        std::format_to(ctx.out(), "{}=", name);

        if (this->pretty) {
            this->p_format(p, ctx);
            std::format_to(ctx.out(), ",");
        } else {
            std::format_to(ctx.out(), "{:?}", p);
            // WARNING: also the last item gets printed with
            std::format_to(ctx.out(), ", ");
        }

        return ctx.out();
    }

    // Print the end of the class
    auto finish(auto &ctx) const {
        if (this->pretty) {
            std::format_to(ctx.out(), "\n{}",
                           std::string(this->spaces - 4, ' '));
        }

        return std::format_to(ctx.out(), ")");
    }
};

template <size_t BUF_SIZE = 4096> struct fd_streambuf : std::streambuf {
    int fd;
    char buf[BUF_SIZE];

    fd_streambuf(int fd) : fd(fd) {}

  protected:
    int_type underflow() override {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n <= 0)
            return traits_type::eof();
        setg(buf, buf, buf + n);
        return traits_type::to_int_type(*gptr());
    }
};

// Template variant for my types. Where the T (inner type of the vector)
// already has debug_spec formatter.
template <typename T, typename CharT>
struct std::formatter<std::vector<T>, CharT> : debug_spec {
    template <typename FormatContext>
    typename FormatContext::iterator format(const std::vector<T> &vec,
                                            FormatContext &ctx) const {
        auto out = ctx.out();
        *out++ = '[';

        for (size_t i = 0; i < vec.size(); ++i) {
            this->field(std::to_string(i), vec[i], ctx);
        }

        if (this->pretty && !vec.empty()) {
            std::format_to(out, "\n{}", std::string(this->spaces - 4, ' '));
        }

        *out++ = ']';
        return out;
    }
};

template <typename T, typename CharT>
struct std::formatter<std::unique_ptr<T>, CharT> : debug_spec {

    template <typename FormatContext>
    typename FormatContext::iterator format(const std::unique_ptr<T> &ptr,
                                            FormatContext &ctx) const {
        if (!ptr) {
            return std::format_to(ctx.out(), "nullptr");
        }

        const T &elem = *ptr;
        return this->iformat(elem, ctx);
    }
};

// template specialization for a std::variant
// whose types already have a formatter.
template <typename... Types>
struct std::formatter<std::variant<Types...>> : debug_spec {
    using VarType = std::variant<Types...>;

    auto format(const VarType &v, auto &ctx) const {
        return std::visit(
            [&]<HasFormatter T>(const T &p) {
                const std::formatter<T> fmt{*this};
                return fmt.format(p, ctx);
            },
            v);
    }
};

// template specialization for a std::optional
template <typename T, typename CharT>
struct std::formatter<std::optional<T>, CharT> : debug_spec {
    template <typename FormatContext>
    typename FormatContext::iterator format(const std::optional<T> &ptr,
                                            FormatContext &ctx) const {
        if (ptr.has_value()) {
            return this->iformat(*ptr, ctx);
        } else {
            return std::format_to(ctx.out(), "None");
        }
    }
};

#endif // TESTSH_UTIL_H
