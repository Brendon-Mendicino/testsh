#ifndef TESTSH_SYNTAX_H
#define TESTSH_SYNTAX_H

#include "tokenizer.h"
#include <string_view>
#include <vector>
#include <format>
#include <variant>
#include <print>
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

struct Script
{
    using Node = std::variant<Program, StatusNeg>;

    Node value;
};

class SyntaxTree
{
public:
    std::optional<Script> build(Tokenizer &tokenizer);

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

template <>
struct std::formatter<Script> : debug_spec
{
    auto format(const Script &v, auto &ctx) const
    {
        return std::visit(
            [&](const auto &p)
            {
                if (this->pretty)
                {
                    const std::string sspaces(this->spaces, ' ');

                    std::format_to(ctx.out(), "Script(\n{}value=", sspaces);
                    this->p_format(p, ctx);
                    return std::format_to(ctx.out(), ")");
                }
                else
                {
                    return std::format_to(ctx.out(), "Script(value={:?})", p);
                }
            },
            v.value);
    }
};

#endif // TESTSH_SYNTAX_H