#ifndef TESTSH_SYNTAX_H
#define TESTSH_SYNTAX_H

#include "tokenizer.h"
#include <string_view>
#include <vector>
#include <format>
#include "../util.h"

struct Program
{
    std::string_view program;
    std::vector<std::string_view> arguments;
};

class SyntaxTree
{
    Tokenizer tokenizer;

public:
    SyntaxTree(std::string_view input);

    Program build();
};

template <>
struct std::formatter<Program> : debug_spec
{
    auto format(const Program &prog, auto &ctx) const
    {
        // Print regex pattern and token type
        return std::format_to(
            ctx.out(),
            // TODO: compiler state is garbage...
            // "Token(type={}, value={:?}, start={}, end={})",
            "Program(program={}, arguments={})",
            prog.program,
            prog.arguments);
    }
};

#endif // TESTSH_SYNTAX_H