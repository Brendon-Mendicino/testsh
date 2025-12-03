#include "tokenizer.h"
#include "re2/re2.h"
#include <vector>
#include <format>
#include <print>
#include <ranges>
#include <cassert>

namespace vw = std::ranges::views;

static const std::vector<Specification> specs{
    // Separators
    {R"(^( +))", TokenType::separator},
    {R"(^(\\)\n$)", TokenType::line_continuation},

    // Subshell
    {R"(^(\())", TokenType::open_round},
    {R"(^(\)))", TokenType::close_round},

    // Command substitution
    {R"(^(\$\())", TokenType::andopen},

    // List separators
    {R"(^(;))", TokenType::semicolon},
    {R"(^(&&))", TokenType::and_and},
    {R"(^(\|\|))", TokenType::or_or},

    // New line
    {R"(^(\n))", TokenType::new_line},

    // IO Number:
    // This number must attached to the redirect operators beginning.
    // Must have greater precedence than number otherwise it will be
    // matched before this one.
    {R"(^(\d+)(?:<|>))", TokenType::io_number},

    // Word kinds
    // Match any normal character + any quoted character
    {R"(^((?:[\w\-\/.]|\\.)+))", TokenType::word},

    // Quoatations
    {R"(^('[^']*'))", TokenType::quoted_word},
    // {R"(^("[^']"))", TokenType::string},

    // Bang
    {R"(^(!))", TokenType::bang},

    // Redirections
    {R"(^(\|))", TokenType::pipe},
    {R"(^(<>))", TokenType::lessgreat},
    {R"(^(<&))", TokenType::lessand},
    {R"(^(>&))", TokenType::greatand},
    {R"(^(>>))", TokenType::dgreat},
    {R"(^(>))", TokenType::great},
    {R"(^(<<))", TokenType::dless},
    {R"(^(<))", TokenType::less},

    // EOF
    {R"(^(\z))", TokenType::eof},
};

// ------------------------------------
// Token
// ------------------------------------

std::string Token::text() const
{
    std::string retval;

    switch (type)
    {
    case TokenType::word:
        std::ranges::copy(
            value | vw::filter([](const char c)
                               { return c != '\\'; }),
            std::back_inserter(retval));
        break;

    case TokenType::quoted_word:
        retval = value.substr(1, value.size() - 2);
        break;

    default:
        retval = value;
        break;
    }

    return retval;
}

// ------------------------------------
// UnbufferedTokenizer
// ------------------------------------

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
// TOKENIZER
// ------------------------------------

Tokenizer::Tokenizer(const TokState &state) : state(state), prev_state() {}

void Tokenizer::update_prev()
{
    this->prev_state = this->state;
}

/**
 * Advances the `buffered_input` and stores the leftover string
 * into a new UnbufferedTokenizer. The previous tokenizer must
 * be exausted before calling this method.
 *
 * Returns false if the buffer was empty.
 */
bool Tokenizer::advance_buffer()
{
    if (this->state.buffered_input.size() == 0)
        return false;

    assertm(this->state.inner_tokenizer.peek().value().type == TokenType::eof,
            "The inner tokenizer must be empty before advancing the line buffer!");

    this->state.inner_tokenizer = UnbufferedTokenizer{this->state.buffered_input[0]};

    this->state.buffered_input = this->state.buffered_input.subspan(1);

    return true;
}

Tokenizer::Tokenizer(std::span<std::string> buffered_input) : state{.buffered_input = buffered_input, .inner_tokenizer = {""}} {}

size_t Tokenizer::buffer_size() const
{
    // +1 because the last buffer is contained inside the UnbufferedTokenizer
    return this->state.buffered_input.size() + 1;
}

std::optional<Tokenizer> Tokenizer::prev() const
{
    if (!this->prev_state)
        return std::nullopt;

    return Tokenizer{*prev_state};
}

std::optional<Token> Tokenizer::next_token()
{
    this->update_prev();
    auto token = this->state.inner_tokenizer.next_token();

    while (token.has_value() && token->type == TokenType::eof)
    {
        if (!this->advance_buffer())
            break;

        token = this->state.inner_tokenizer.next_token();
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

// ------------------------------------
// TokenIter
// ------------------------------------

TokenIter::TokenIter(std::span<Token> tokens) : tokens(tokens) {}

std::optional<Token> TokenIter::next_token()
{
    if (this->tokens.empty())
        return std::nullopt;

    const Token token = this->tokens.front();

    this->tokens = this->tokens.subspan(1);

    return token;
}

bool TokenIter::next_is_eof() const
{
    return this->tokens.empty();
}

std::optional<Token> TokenIter::peek() const
{
    if (this->tokens.empty())
        return std::nullopt;

    return this->tokens.front();
}