#ifndef TESTSH_TOKENIZER_H
#define TESTSH_TOKENIZER_H

#include <string>
#include <string_view>
#include <format>
#include <optional>
#include <utility>
#include "../util.h"

enum class TokenType
{
    argument,
    string,
    separator,
    new_line,
    semicolon,
    eof,
};

struct Token
{
    TokenType type;
    std::string value;
    size_t start;
    size_t end;
};

struct Specification
{
    std::string regex;
    TokenType spec_type;
};

class Tokenizer
{
    std::string_view input;
    size_t string_offset = 0;
    bool eof_reached = false;

public:
    Tokenizer(std::string_view input);

    std::optional<Token> next_token();
};

constexpr std::string_view to_string(const TokenType token)
{
    switch (token)
    {
    case TokenType::argument:
        return "argument";
    case TokenType::separator:
        return "separator";
    case TokenType::string:
        return "string";
    case TokenType::new_line:
        return "new_line";
    case TokenType::semicolon:
        return "semicolon";
    case TokenType::eof:
        return "eof";
    }

    std::unreachable();
}

template <>
struct std::formatter<Token> : debug_spec
{
    auto format(const Token &token, auto &ctx) const
    {
        // Print regex pattern and token type
        return std::format_to(
            ctx.out(),
            // compiler state is garbage...
            // "Token(type={}, value={:?}, start={}, end={})",
            "Token(type={}, value={}, start={}, end={})",
            to_string(token.type),
            token.value,
            token.start,
            token.end);
    }
};

template <>
struct std::formatter<Specification> : debug_spec
{
    auto format(const Specification &spec, auto &ctx) const
    {
        // Print regex pattern and token type
        return std::format_to(
            ctx.out(),
            "Specification(regex={}, type={})",
            spec.regex,
            to_string(spec.spec_type));
    }
};

#endif // TESTSH_TOKENIZER_H