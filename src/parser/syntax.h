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

struct Program
{
    std::string_view program;
    std::vector<std::string_view> arguments;
};

struct StatusNeg
{
    Program prog;
};

using Words = std::variant<Program, StatusNeg>;

struct AndList;
struct OrList;

using OpList = std::variant<AndList, OrList, Words>;

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
    std::unique_ptr<OpList> left;
    optional_ptr<OpList> right;
};

class SyntaxTree
{
    template <typename VariantType, typename Fn>
    inline std::optional<VariantType> check(Tokenizer &tokenizer, Fn fn) const;

public:
    std::optional<OpList> build(Tokenizer &tokenizer);

    std::optional<OpList> op_list(Tokenizer &tokenizer) const;

    std::optional<SequentialList> sequential_list(Tokenizer &tokenizer) const;

    std::optional<Words> words(Tokenizer &tokenizer) const;

    std::optional<Program> program(Tokenizer &tokenizer) const;

    std::optional<StatusNeg> status_neg(Tokenizer &tokenzier) const;
};

// ----------------
// FOMATTING
// ----------------

template <>
struct std::formatter<Program> : debug_spec
{
    auto format(const Program &prog, auto &ctx) const
    {
        if (this->pretty)
        {
            const std::string sspaces(this->spaces, ' ');

            return std::format_to(
                ctx.out(),
                "Program(\n{}program={},\n{}arguments={})",
                sspaces,
                prog.program,
                sspaces,
                prog.arguments);
        }
        else
        {
            return std::format_to(
                ctx.out(),
                "Program(program={}, arguments={})",
                prog.program,
                prog.arguments);
        }
    }
};

template <>
struct std::formatter<StatusNeg> : debug_spec
{
    auto format(const StatusNeg &prog, auto &ctx) const
    {
        if (this->pretty)
        {
            const std::string sspaces(this->spaces, ' ');

            std::format_to(ctx.out(), "StatusNeg(\n{}program=", sspaces);
            this->p_format(prog.prog, ctx);
            return std::format_to(ctx.out(), ")");
        }
        else
        {
            return std::format_to(
                ctx.out(),
                "StatusNeg(program={:?})",
                prog.prog);
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