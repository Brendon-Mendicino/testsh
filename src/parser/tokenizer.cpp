#include "tokenizer.h"
#include "re2/re2.h"
#include <vector>
#include <format>
#include <print>
#include <cassert>

static const std::vector<Specification> specs{
    // Separators
    {R"(^( +))", TokenType::separator},

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
    {R"(^([\w\-_\/]+))", TokenType::word},

    {R"(^("[^']"))", TokenType::string},
    {R"(^('[^']'))", TokenType::string},

    {R"(^(!))", TokenType::bang},

    {R"(^(\z))", TokenType::eof},
};

Tokenizer::Tokenizer(std::string_view input) : input(input) {}

std::optional<Token> Tokenizer::next_token()
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

std::optional<Token> Tokenizer::next_token_with_separators()
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

bool Tokenizer::next_is_eof() const
{
    if(const auto next = this->peek())
        return next->type == TokenType::eof;

    return false;
}

std::optional<Token> Tokenizer::peek() const
{
    Tokenizer copy{*this};

    return copy.next_token();
}