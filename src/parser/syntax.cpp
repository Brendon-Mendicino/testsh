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

std::optional<SequentialList> SyntaxTree::build(Tokenizer &tokenizer)
{
    return this->complete_command(tokenizer);
}

/**
 * BNF:
 *
 * ```
 * complete_command ::= sequential_list
 *                    | sequential_list SEMI
 *                    ;
 * ```
 *
 * @param tokenizer
 * @return std::optional<SequentialList>
 */
std::optional<SequentialList> SyntaxTree::complete_command(Tokenizer &tokenizer) const
{
    auto seq_list = this->sequential_list(tokenizer);
    if (!seq_list.has_value())
        return std::nullopt;

    const auto semi = tokenizer.peek();
    if (semi.has_value() && semi->type == TokenType::semicolon)
    {
        // Advance the tokenizer if the next token is a SEMI
        tokenizer.next_token();
    }

    return seq_list;
}

/**
 * BNF:
 *
 * ```
 * sequential_list ::= op_list
 *                   | sequential_list SEMI op_list
 *                   ;
 * ```
 *
 * @param tokenizer
 * @return std::optional<SequentialList>
 */
std::optional<SequentialList> SyntaxTree::sequential_list(Tokenizer &tokenizer) const
{
    SequentialList retval{};

    auto first_op_list = this->op_list(tokenizer);
    if (!first_op_list.has_value())
        return std::nullopt;

    retval.right = std::make_unique<OpList>(std::move(*first_op_list));

    // Check if there are other sequential list to concatenate to the return value
    for (;;)
    {
        // If the next token is not what we expect don't advance the tokenizer sequence
        Tokenizer sub_token{tokenizer};

        const auto semi = sub_token.next_token();
        if (!semi.has_value())
        {
            break;
        }

        if (semi->type != TokenType::semicolon)
        {
            break;
        }

        auto next_op_list = this->op_list(sub_token);
        if (!next_op_list.has_value())
        {
            break;
        }

        SequentialList rotated_list{
            .left = std::make_unique<SequentialList>(std::move(retval)),
            .right = std::make_unique<OpList>(std::move(*next_op_list)),
        };

        retval = std::move(rotated_list);

        tokenizer = sub_token;
    }

    return retval;
}

/**
 * BNF:
 *
 * ```
 * op_list ::= words
 *           | op_list AND_AND words
 *           | op_list OR_OR words
 *           ;
 * ```
 *
 * @param tokenizer
 * @return std::optional<OpList>
 */
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