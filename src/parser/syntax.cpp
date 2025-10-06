#include "syntax.h"
#include <print>

template <typename ListFn>
inline std::optional<List> SyntaxTree::variant_list(Tokenizer &tokenizer, ListFn fn) const
{
    Tokenizer sub_tok{tokenizer};

    if (auto list = (this->*fn)(sub_tok))
    {
        if (sub_tok.next_is_eof())
        {
            tokenizer = sub_tok;
            return List{std::move(*list)};
        }
    }

    return std::nullopt;
}

std::optional<List> SyntaxTree::build(Tokenizer &tokenizer)
{
    return this->list(tokenizer);
}

std::optional<List> SyntaxTree::list(Tokenizer &tokenizer) const
{
    if (auto words = this->variant_list(tokenizer, &SyntaxTree::words))
        return words;

    if (auto and_list = this->variant_list(tokenizer, &SyntaxTree::and_list))
        return and_list;

    if (auto or_list = this->variant_list(tokenizer, &SyntaxTree::or_list))
        return or_list;

    if (auto sequential_list = this->variant_list(tokenizer, &SyntaxTree::sequential_list))
        return sequential_list;

    return std::nullopt;
}

std::optional<AndList> SyntaxTree::and_list(Tokenizer &tokenizer) const
{
    auto left = this->words(tokenizer);
    if (!left.has_value())
        return std::nullopt;

    const auto sep = tokenizer.next_token();
    if (!sep.has_value())
        return std::nullopt;

    if (sep->type != TokenType::and_and)
        return std::nullopt;

    auto right = this->list(tokenizer);
    if (!right.has_value())
        return std::nullopt;

    return AndList{
        .left = std::make_unique<List>(std::move(*left)),
        .right = std::make_unique<List>(std::move(*right)),
    };
}

std::optional<OrList> SyntaxTree::or_list(Tokenizer &tokenizer) const
{
    auto left = this->words(tokenizer);
    if (!left.has_value())
        return std::nullopt;

    const auto sep = tokenizer.next_token();
    if (!sep.has_value())
        return std::nullopt;

    if (sep->type != TokenType::or_or)
        return std::nullopt;

    auto right = this->list(tokenizer);
    if (!right.has_value())
        return std::nullopt;

    return OrList{
        .left = std::make_unique<List>(std::move(*left)),
        .right = std::make_unique<List>(std::move(*right)),
    };
}

// TODO: modify
std::optional<SequentialList> SyntaxTree::sequential_list(Tokenizer &tokenizer) const
{
    auto left = this->words(tokenizer);
    if (!left.has_value())
        return std::nullopt;

    const auto sep = tokenizer.next_token();
    if (!sep.has_value())
        return std::nullopt;

    if (sep->type != TokenType::semicolon)
        return std::nullopt;

    auto right = this->list(tokenizer);
    if (!right.has_value())
        return std::nullopt;

    return SequentialList{
        .left = std::make_unique<List>(std::move(*left)),
        .right = std::make_unique<List>(std::move(*right)),
    };
}

std::optional<Words> SyntaxTree::words(Tokenizer &tokenizer) const
{
    Tokenizer sub_tok{tokenizer};
    if (auto prog_neg = this->status_neg(sub_tok))
    {
        tokenizer = sub_tok;
        return *prog_neg;
    }

    sub_tok = Tokenizer{tokenizer};
    if (const auto prog = this->program(sub_tok))
    {
        tokenizer = sub_tok;
        return *prog;
    }

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