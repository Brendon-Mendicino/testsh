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
    std::string_view program;
    std::vector<std::string_view> arguments;
    std::vector<Redirect> redirections;
};

struct AndList;
struct OrList;
struct Subshell;
struct Pipeline;

using Command = std::variant<SimpleCommand, Subshell>;
using OpList = std::variant<AndList, OrList, Pipeline>;

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
    std::optional<ThisProgram> build(Tokenizer &tokenizer);

    std::optional<CompleteCommands> complete_commands(Tokenizer &tokenizer) const;

    std::optional<SequentialList> complete_command(Tokenizer &tokenizer) const;

    std::optional<SequentialList> list(Tokenizer &tokenizer) const;

    std::optional<OpList> op_list(Tokenizer &tokenizer) const;

    std::optional<Pipeline> pipeline(Tokenizer &tokenizer) const;

    std::optional<Pipeline> pipe_sequence(Tokenizer &tokenizer) const;

    std::optional<Command> command(Tokenizer &tokenizer) const;

    std::optional<Subshell> compound_command(Tokenizer &tokenizer) const;

    std::optional<Subshell> subshell(Tokenizer &tokenizer) const;

    std::optional<SimpleCommand> simple_command(Tokenizer &tokenizer) const;

    std::optional<std::vector<Redirect>> redirect_list(Tokenizer &tokenizer) const;

    std::optional<Redirect> io_redirect(Tokenizer &tokenizer) const;

    std::optional<Redirect> io_file(Tokenizer &tokenizer) const;

    std::optional<Redirect> io_here(Tokenizer &tokenizer) const;

    std::optional<std::string_view> filename(Tokenizer &tokenizer) const;

    bool newline_list(Tokenizer &tokenizer) const;

    bool linebreak(Tokenizer &tokenizer) const;

    std::optional<std::string_view> word(Tokenizer &tokenizer) const;
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
        if (this->pretty)
        {
            const std::string sspaces(this->spaces, ' ');

            std::format_to(
                ctx.out(),
                "SimpleCommand(\n{}program={},\n{}arguments={},\n{}redirections=",
                sspaces,
                prog.program,
                sspaces,
                prog.arguments,
                sspaces);
            this->p_format(prog.redirections, ctx);
            return std::format_to(ctx.out(), ")");
        }
        else
        {
            return std::format_to(
                ctx.out(),
                "SimpleCommand(program={}, arguments={}, redirections={:?})",
                prog.program,
                prog.arguments,
                prog.redirections);
        }
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