#include "tokenizer.h"
#include "re2/re2.h"
#include <vector>
#include <format>
#include <print>
#include <cassert>

static const std::vector<Specification> specs{
    // Separators
    {R"(^( +))", TokenType::separator},
    {R"(^(\n))", TokenType::new_line},
    {R"(^(;))", TokenType::semicolon},

    // Argument kinds
    {R"(^(\w+))", TokenType::argument},

    {R"(^("[^']"))", TokenType::string},
    {R"(^('[^']'))", TokenType::string},
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
            .value = std::string{match},
            .start = this->string_offset,
            .end = this->string_offset + match.length()};

        assert(match.length() <= this->input.length());

        this->input = this->input.substr(match.length());
        this->string_offset += match.length();

        return token;
    }

    if (this->input.length() == 0 && !this->eof_reached)
    {
        this->eof_reached = true;

        return Token{
            .type = TokenType::eof,
            .value = "",
            .start = this->string_offset,
            .end = this->string_offset};
    }

    return std::nullopt;
}