#include "syntax.h"
#include <print>

std::optional<Script> SyntaxTree::build(Tokenizer &tokenizer)
{
    Tokenizer sub_tok{tokenizer};
    if (const auto prog_neg = this->status_neg(sub_tok))
        return Script{*prog_neg};

    sub_tok = Tokenizer{tokenizer};
    if (const auto prog = this->program(sub_tok))
        return Script{*prog};

    return std::nullopt;
}

std::optional<Program> SyntaxTree::program(Tokenizer &tokenizer) const
{
    const auto prog_token{tokenizer.next_token()};

    if (!prog_token.has_value())
        return std::nullopt;

    if (prog_token->type != TokenType::word)
        return std::nullopt;

    std::vector<std::string_view> args{};

    while (const auto arg = tokenizer.peek())
    {
        if (arg->type != TokenType::word)
            break;

        args.emplace_back(arg->value);

        tokenizer.next_token();
    }

    return Program{prog_token->value, std::move(args)};
}

std::optional<StatusNeg> SyntaxTree::status_neg(Tokenizer &tokenzier) const
{
    const auto maybe_bang{tokenzier.next_token()};

    if (!maybe_bang.has_value())
        return std::nullopt;

    if (maybe_bang->type != TokenType::bang)
        return std::nullopt;

    const auto maybe_prog{this->program(tokenzier)};

    if (!maybe_prog.has_value())
        return std::nullopt;

    return StatusNeg{std::move(*maybe_prog)};
}