#include "tokenizer.h"
#include "re2/re2.h"
#include <vector>
#include <format>
#include <print>
#include <cassert>

static const std::vector<Specification> specs{
    // Separators
    {R"(^( +))", TokenType::separator},
    {R"(^(\\)$)", TokenType::line_continuation},

    // Subshell
    {R"(^(\())", TokenType::open_round},
    {R"(^(\)))", TokenType::close_round},

    // List separators
    {R"(^(;))", TokenType::semicolon},
    {R"(^(&&))", TokenType::and_and},
    {R"(^(\|\|))", TokenType::or_or},

    {R"(^(\n))", TokenType::new_line},

    // word kinds
    {R"(^(\d+))", TokenType::number},
    {R"(^([\w\-_\/.]+))", TokenType::word},

    {R"(^("[^']"))", TokenType::string},
    {R"(^('[^']'))", TokenType::string},

    {R"(^(!))", TokenType::bang},

    {R"(^(\z))", TokenType::eof},
};

// ------------------------------------
// UnbufferedTokenizer
// ------------------------------------

UnbufferedTokenizer::UnbufferedTokenizer(std::string_view input) : input(input) {}

std::optional<Token> UnbufferedTokenizer::next_token()
{
    std::string_view match{};

    for (const auto &spec : specs)
    {
        if (!RE2::PartialMatch(this->input, spec.regex, &match))
            continue;

        const Token token{
            .type = spec.spec_type,
            .value = match,
            .start = this->string_offset,
            .end = this->string_offset + match.length()};

        assert(match.length() <= this->input.length());

        this->input = this->input.substr(match.length());
        this->string_offset += match.length();

        if (token.type == TokenType::separator)
            continue;

        return token;
    }

    return std::nullopt;
}

std::optional<Token> UnbufferedTokenizer::next_token_with_separators()
{
    std::string_view match{};

    for (const auto &spec : specs)
    {
        if (!RE2::PartialMatch(this->input, spec.regex, &match))
            continue;

        const Token token{
            .type = spec.spec_type,
            .value = match,
            .start = this->string_offset,
            .end = this->string_offset + match.length()};

        assert(match.length() <= this->input.length());

        this->input = this->input.substr(match.length());
        this->string_offset += match.length();

        return token;
    }

    return std::nullopt;
}

bool UnbufferedTokenizer::next_is_eof() const
{
    if (const auto next = this->peek())
        return next->type == TokenType::eof;

    return false;
}

std::optional<Token> UnbufferedTokenizer::peek() const
{
    UnbufferedTokenizer copy{*this};

    return copy.next_token();
}

// ------------------------------------
// tokenizer
// ------------------------------------

/**
 * Advances the `buffered_input` and stores the leftover string
 * into a new UnbufferedTokenizer. The previous tokenizer must
 * be exausted before calling this method.
 * 
 * Returns false if the buffer was empty.
 */
bool Tokenizer::advance_buffer()
{
    if (this->buffered_input.size() == 0)
        return false;

    assertm(this->inner_tokenizer.next_token().value().type == TokenType::eof, "The inner tokenizer must be empty before advancing the line buffer!");

    this->inner_tokenizer = UnbufferedTokenizer{this->buffered_input[0]};

    this->buffered_input = this->buffered_input.subspan(1);

    return true;
}

Tokenizer::Tokenizer(std::span<std::string> buffered_input) : buffered_input(buffered_input), inner_tokenizer("") {}

std::optional<Token> Tokenizer::next_token()
{
    auto token = this->inner_tokenizer.next_token();

    while (token.has_value() && token->type == TokenType::eof)
    {
        if (!this->advance_buffer())
            break;

        token = this->inner_tokenizer.next_token();
    }

    return token;
}

std::optional<Token> Tokenizer::next_token_with_separators()
{
    auto token = this->inner_tokenizer.next_token_with_separators();

    while (token.has_value() && token->type == TokenType::eof)
    {
        if (!this->advance_buffer())
            break;

        token = this->inner_tokenizer.next_token_with_separators();
    }

    return token;
}

bool Tokenizer::next_is_eof() const
{
    if (const auto next = this->peek())
        return next->type == TokenType::eof;

    return false;
}

std::optional<Token> Tokenizer::peek() const
{
    Tokenizer copy{*this};

    return copy.next_token();
}