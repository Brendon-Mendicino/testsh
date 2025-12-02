#ifndef TESTSH_SYNTAX_H
#define TESTSH_SYNTAX_H

#include "tokenizer.h"
#include <string_view>
#include <vector>
#include <format>
#include <variant>
#include <print>
#include <memory>
#include <utility>
#include "../util.h"

enum class OpenKind
{
    read,
    replace,
    append,
    rw,
};

struct FileRedirect
{
    int redirect_fd;
    OpenKind file_kind;
    std::string_view filename;
};

struct FdRedirect
{
    int fd_to_replace;
    int fd_replacer;
};

struct CloseFd
{
    int fd;
};

using Redirect = std::variant<FileRedirect, FdRedirect, CloseFd>;

struct SimpleCommand
{
    Token program;
    // TODO: this should be Tokens
    std::vector<Token> arguments;
    std::vector<Redirect> redirections;
};

struct AndList;
struct OrList;
struct Subshell;
struct Pipeline;

using Command = std::variant<SimpleCommand, Subshell>;
using OpList = std::variant<AndList, OrList, Pipeline>;
// using List = std::variant<SequentialList, AsyncList>;

struct AndList
{
    std::unique_ptr<OpList> left;
    std::unique_ptr<OpList> right;
};

struct OrList
{
    std::unique_ptr<OpList> left;
    std::unique_ptr<OpList> right;
};

struct SequentialList
{
    optional_ptr<SequentialList> left;
    std::unique_ptr<OpList> right;
};

struct Pipeline
{
    optional_ptr<Pipeline> left;
    std::unique_ptr<Command> right;
    // TODO: remember to add this to the formatter
    bool negated;
};

struct Subshell
{
    std::unique_ptr<SequentialList> seq_list;
    std::vector<Redirect> redirections;
};

using CompleteCommands = std::vector<SequentialList>;

struct ThisProgram
{
    optional_ptr<CompleteCommands> child;
};

// ---------------------------
// SyntaxTree
// ---------------------------

/**
 * @brief You can read the full bash syntax BNF at
 * https://pubs.opengroup.org/onlinepubs/9699919799/utilities/V3_chap02.html#tag_18_10
 *
 */
class SyntaxTree
{
    template <typename VariantType, typename Fn>
    inline std::optional<VariantType> check(Tokenizer &tokenizer, Fn fn) const;

public:
    std::optional<ThisProgram> program_substitution(Tokenizer &tokenizer) const;

    std::optional<ThisProgram> program(Tokenizer &tokenizer) const;

    std::optional<CompleteCommands> complete_commands(Tokenizer &tokenizer) const;

    std::optional<SequentialList> complete_command(Tokenizer &tokenizer) const;

    std::optional<SequentialList> list(Tokenizer &tokenizer) const;

    std::optional<OpList> and_or(Tokenizer &tokenizer) const;

    std::optional<Pipeline> pipeline(Tokenizer &tokenizer) const;

    std::optional<Pipeline> pipe_sequence(Tokenizer &tokenizer) const;

    std::optional<Command> command(Tokenizer &tokenizer) const;

    std::optional<Subshell> compound_command(Tokenizer &tokenizer) const;

    std::optional<Subshell> subshell(Tokenizer &tokenizer) const;

    std::optional<SequentialList> compound_list(Tokenizer &tokenizer) const;

    std::optional<SequentialList> term(Tokenizer &tokenizer) const;

    std::optional<SimpleCommand> simple_command(Tokenizer &tokenizer) const;

    std::optional<std::vector<Redirect>> redirect_list(Tokenizer &tokenizer) const;

    std::optional<Redirect> io_redirect(Tokenizer &tokenizer) const;

    std::optional<Redirect> io_file(Tokenizer &tokenizer) const;

    std::optional<Redirect> io_here(Tokenizer &tokenizer) const;

    std::optional<std::string_view> filename(Tokenizer &tokenizer) const;

    bool newline_list(Tokenizer &tokenizer) const;

    void linebreak(Tokenizer &tokenizer) const;

    std::optional<Token> word(Tokenizer &tokenizer) const;

    inline std::optional<Token> token(Tokenizer &tokenizer, const TokenType type) const;
};

class ArgsToExec
{
    typedef const char *args_array_t[];

    std::vector<std::string> str_owner;
    std::unique_ptr<args_array_t> args_array;
    std::size_t args_size;

public:
    explicit ArgsToExec(const SimpleCommand &cmd)
    {
        namespace vw = std::ranges::views;

        const auto &args = cmd.arguments;

        // +1 from program
        // +1 from NULL terminator
        this->args_size = args.size() + 2;

        this->args_array = std::make_unique<args_array_t>(this->args_size);

        this->str_owner.reserve(args.size() + 2);

        // Push program first
        this->str_owner.emplace_back(cmd.program.text());
        this->args_array[0] = this->str_owner[0].c_str();

        for (size_t i{}; i < args.size(); ++i)
        {
            auto &s = this->str_owner.emplace_back(args[i].text());
            this->args_array[i + 1] = s.c_str();
        }

        // The args array needs to be null-terminated
        this->args_array[this->args_size - 1] = nullptr;
    }

    [[nodiscard]] auto args() const &
    {
        return &this->args_array;
    }
};


// ----------------
// FOMATTING
// ----------------

constexpr std::string_view to_string(const OpenKind kind)
{
    switch (kind)
    {
    case OpenKind::read:
        return "read";
    case OpenKind::replace:
        return "replace";
    case OpenKind::append:
        return "append";
    case OpenKind::rw:
        return "rw";
    }
}

template <>
struct std::formatter<FileRedirect> : debug_spec
{
    auto format(const FileRedirect &red, auto &ctx) const
    {
        if (this->pretty)
        {
            const std::string sspaces(this->spaces, ' ');

            return std::format_to(
                ctx.out(),
                "FileRedirect(\n{}redirect_fd={},\n{}file_kind={},\n{}filaname={})",
                sspaces,
                red.redirect_fd,
                sspaces,
                to_string(red.file_kind),
                sspaces,
                red.filename);
        }
        else
        {
            return std::format_to(
                ctx.out(),
                "FileRedirect(redirect_fd={}, file_kind={}, filaname={})",
                red.redirect_fd,
                to_string(red.file_kind),
                red.filename);
        }
    }
};

template <>
struct std::formatter<FdRedirect> : debug_spec
{
    auto format(const FdRedirect &red, auto &ctx) const
    {
        if (this->pretty)
        {
            const std::string sspaces(this->spaces, ' ');

            return std::format_to(
                ctx.out(),
                "FdRedirect(\n{}fd_to_replace={},\n{}fd_replacer={})",
                sspaces,
                red.fd_to_replace,
                sspaces,
                red.fd_replacer);
        }
        else
        {
            return std::format_to(
                ctx.out(),
                "FdRedirect(fd_to_replace={}, fd_replacer={})",
                red.fd_to_replace,
                red.fd_replacer);
        }
    }
};

template <>
struct std::formatter<CloseFd> : debug_spec
{
    auto format(const CloseFd &red, auto &ctx) const
    {
        if (this->pretty)
        {
            const std::string sspaces(this->spaces, ' ');

            return std::format_to(
                ctx.out(),
                "CloseFd(\n{}fd={})",
                sspaces,
                red.fd);
        }
        else
        {
            return std::format_to(
                ctx.out(),
                "CloseFd(fd={})",
                red.fd);
        }
    }
};

template <>
struct std::formatter<SimpleCommand> : debug_spec
{
    auto format(const SimpleCommand &prog, auto &ctx) const
    {
        this->start<SimpleCommand>(ctx);
        this->field("program", prog.program, ctx);
        this->field("arguments", prog.arguments, ctx);
        this->field("redirections", prog.redirections, ctx);
        return this->finish(ctx);
    }
};

template <>
struct std::formatter<Subshell> : debug_spec
{
    auto format(const Subshell &subshell, auto &ctx) const
    {
        if (this->pretty)
        {
            const std::string sspaces(this->spaces, ' ');

            std::format_to(ctx.out(), "Subshell(\n{}seq_list=", sspaces);
            this->p_format(subshell.seq_list, ctx);
            std::format_to(ctx.out(), "\n{}redirections=", sspaces);
            this->p_format(subshell.redirections, ctx);
            return std::format_to(ctx.out(), ")");
        }
        else
        {
            return std::format_to(
                ctx.out(),
                "Subshell(seq_list={:?}, redirections={:?})",
                subshell.seq_list,
                subshell.redirections);
        }
    }
};

template <typename T>
concept HasChild = requires(T t) {
    t.child;
};

template <HasChild T>
struct std::formatter<T> : debug_spec
{
    auto format(const T &node, auto &ctx) const
    {
        if (this->pretty)
        {
            const std::string sspaces(this->spaces, ' ');

            std::format_to(ctx.out(), "{}(\n{}child=", typeid_name<T>(), sspaces);
            this->p_format(node.child, ctx);
            return std::format_to(ctx.out(), ")");
        }
        else
        {
            return std::format_to(
                ctx.out(),
                "{}(child={:?})",
                typeid_name<T>(),
                node.child);
        }
    }
};

template <typename T>
concept HasLeftRight = requires(T t) {
    t.left;
    t.right;
};

template <HasLeftRight T>
struct std::formatter<T> : debug_spec
{
    auto format(const T &node, auto &ctx) const
    {
        if (this->pretty)
        {
            const std::string sspaces(this->spaces, ' ');

            std::format_to(ctx.out(), "{}(\n{}left=", typeid_name<T>(), sspaces);
            this->p_format(node.left, ctx);
            std::format_to(ctx.out(), ",\n{}right=", sspaces);
            this->p_format(node.right, ctx);
            return std::format_to(ctx.out(), ")");
        }
        else
        {
            return std::format_to(
                ctx.out(),
                "{}(left={:?}, right={:?})",
                typeid_name<T>(),
                node.left,
                node.right);
        }
    }
};

#endif // TESTSH_SYNTAX_H