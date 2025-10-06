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
    number,
    word,
    string,
    separator,
    new_line,
    semicolon,
    and_and,
    or_or,
    bang,
    open_paren,
    close_paren,
    eof,
};

struct Token
{
    TokenType type;
    std::string_view value;
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

public:
    Tokenizer(std::string_view input);

    std::optional<Token> next_token();

    std::optional<Token> next_token_with_separators();

    bool next_is_eof() const;

    std::optional<Token> peek() const;
};

constexpr std::string_view to_string(const TokenType token)
{
    switch (token)
    {
    case TokenType::number:
        return "number";
    case TokenType::word:
        return "word";
    case TokenType::separator:
        return "separator";
    case TokenType::string:
        return "string";
    case TokenType::new_line:
        return "new_line";
    case TokenType::semicolon:
        return "semicolon";
    case TokenType::and_and:
        return "and_and";
    case TokenType::or_or:
        return "or_or";
    case TokenType::bang:
        return "bang";
    case TokenType::open_paren:
        return "open_paren";
    case TokenType::close_paren:
        return "close_paren";
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
            // TODO: compiler state is garbage...
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
            // TODO: compiler state is garbage...
            // "Specification(regex={:?}, type={})",
            "Specification(regex={}, type={})",
            spec.regex,
            to_string(spec.spec_type));
    }
};

#endif // TESTSH_TOKENIZER_H