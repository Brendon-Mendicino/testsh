#include "syntax.h"
#include <print>

template <typename VariantType, typename Fn>
inline std::optional<VariantType> SyntaxTree::check(Tokenizer &tokenizer, Fn fn) const
{
    Tokenizer sub_tok{tokenizer};

    if (auto list = (this->*fn)(sub_tok))
    {
        tokenizer = sub_tok;
        return VariantType{std::move(*list)};
    }

    return std::nullopt;
}

std::optional<OpList> SyntaxTree::build(Tokenizer &tokenizer)
{
    return this->op_list(tokenizer);
}

std::optional<SequentialList> SyntaxTree::sequential_list(Tokenizer &tokenizer) const
{
    auto left = this->op_list(tokenizer);
    if (!left.has_value())
        return std::nullopt;

    const auto semi = tokenizer.next_token();
    if (!semi.has_value())
        return std::nullopt;

    if (semi->type != TokenType::semicolon)
        return std::nullopt;

    auto right = this->op_list(tokenizer);

    return SequentialList{
        .left = std::make_unique<OpList>(std::move(*left)),
        .right =
            !right.has_value()
                ? std::nullopt
                : make_optptr<OpList>(std::move(*right)),
    };
}

std::optional<OpList> SyntaxTree::op_list(Tokenizer &tokenizer) const
{
    if (auto and_list = this->check<AndList>(tokenizer, &SyntaxTree::and_list))
        return and_list;

    if (auto or_list = this->check<OrList>(tokenizer, &SyntaxTree::or_list))
        return or_list;

    if (auto words = this->check<Words>(tokenizer, &SyntaxTree::words))
        return words;

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

    auto right = this->op_list(tokenizer);
    if (!right.has_value())
        return std::nullopt;

    return AndList{
        .left = std::make_unique<OpList>(std::move(*left)),
        .right = std::make_unique<OpList>(std::move(*right)),
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

    auto right = this->op_list(tokenizer);
    if (!right.has_value())
        return std::nullopt;

    return OrList{
        .left = std::make_unique<OpList>(std::move(*left)),
        .right = std::make_unique<OpList>(std::move(*right)),
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