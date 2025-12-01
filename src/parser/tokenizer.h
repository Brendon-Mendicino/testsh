#ifndef TESTSH_TOKENIZER_H
#define TESTSH_TOKENIZER_H

#include <string>
#include <string_view>
#include <format>
#include <optional>
#include <utility>
#include <span>
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
    pipe,
    io_number,
    less,
    great,
    dless,
    dgreat,
    lessand,
    greatand,
    lessgreat,
    dlessdash,
    open_round,
    close_round,
    andopen,
    line_continuation,
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

/**
 * Use UnbufferedTokenizer to process a single line of
 * the user input.
 */
struct UnbufferedTokenizer
{
    std::string_view input;
    size_t string_offset = 0;


    std::optional<Token> next_token();

    bool next_is_eof() const;

    std::optional<Token> peek() const;
};

struct TokState
{
    std::span<std::string> buffered_input;
    UnbufferedTokenizer inner_tokenizer;
};

class Tokenizer
{
    TokState state;
    std::optional<TokState> prev_state;

    Tokenizer(const TokState &state);
    void update_prev();
    bool advance_buffer();
public:
    Tokenizer(std::span<std::string> buffered_input);

    size_t buffer_size() const;

    std::optional<Tokenizer> prev() const;

    std::optional<Token> next_token();

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
    case TokenType::pipe:
        return "pipe";
    case TokenType::io_number:
        return "io_number";
    case TokenType::less:
        return "less";
    case TokenType::great:
        return "great";
    case TokenType::dless:
        return "dless";
    case TokenType::dgreat:
        return "dgreat";
    case TokenType::lessand:
        return "lessand";
    case TokenType::greatand:
        return "greatand";
    case TokenType::lessgreat:
        return "lessgreat";
    case TokenType::dlessdash:
        return "dlessdash";
    case TokenType::open_round:
        return "open_round";
    case TokenType::close_round:
        return "close_round";
    case TokenType::andopen:
        return "andopen";
    case TokenType::line_continuation:
        return "line_continuation";
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