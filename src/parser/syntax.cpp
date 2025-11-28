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
    auto words = this->words(tokenizer);
    if (!words.has_value())
    {
        return std::nullopt;
    }

    OpList retval = std::move(*words);

    for (;;)
    {
        // Duplicate the tokenizer. By duplicating, if the parsing
        // fails, the advancements are not committed to the original
        // tokenizer.
        Tokenizer sub_tok{tokenizer};

        auto next_token = sub_tok.next_token();
        if (!next_token.has_value())
        {
            break;
        }

        // After this if the token is either and_and or or_or
        if (next_token->type != TokenType::and_and && next_token->type != TokenType::or_or)
        {
            break;
        }

        // The next tokens must be a words
        auto lhs_words = this->words(sub_tok);
        if (!lhs_words.has_value())
        {
            break;
        }

        if (next_token->type == TokenType::and_and)
        {
            AndList and_and{
                .left = std::make_unique<OpList>(std::move(retval)),
                .right = std::make_unique<OpList>(std::move(*lhs_words)),
            };

            retval = std::move(and_and);
        }
        else if (next_token->type == TokenType::or_or)
        {
            OrList or_or{
                .left = std::make_unique<OpList>(std::move(retval)),
                .right = std::make_unique<OpList>(std::move(*lhs_words)),
            };

            retval = std::move(or_or);
        }
        else
        {
            assertm(false, "Should not be here!");
        }

        // If the check passed advance the original tokenizer
        tokenizer = sub_tok;
    }

    return retval;
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