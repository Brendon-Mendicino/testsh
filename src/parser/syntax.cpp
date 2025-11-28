#include "syntax.h"
#include <print>

/**
 * @brief Use this function if you want to check an optional variant.
 * If the return value of the fucntion `fn` is `std::nullopt` then
 * don't commint the advancements to the tokenizer.
 * 
 * @tparam VariantType 
 * @tparam Fn 
 * @param tokenizer 
 * @param fn 
 * @return std::optional<VariantType> 
 */
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
 * op_list ::= command
 *           | op_list AND_AND command
 *           | op_list OR_OR command
 *           ;
 * ```
 *
 * @param tokenizer
 * @return std::optional<OpList>
 */
std::optional<OpList> SyntaxTree::op_list(Tokenizer &tokenizer) const
{
    auto command = this->command(tokenizer);
    if (!command.has_value())
    {
        return std::nullopt;
    }

    OpList retval = std::move(*command);

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

        // The next tokens must be a command
        // TODO: do some error handling?
        auto lhs_command = this->command(sub_tok);
        if (!lhs_command.has_value())
        {
            break;
        }

        if (next_token->type == TokenType::and_and)
        {
            AndList and_and{
                .left = std::make_unique<OpList>(std::move(retval)),
                .right = std::make_unique<OpList>(std::move(*lhs_command)),
            };

            retval = std::move(and_and);
        }
        else if (next_token->type == TokenType::or_or)
        {
            OrList or_or{
                .left = std::make_unique<OpList>(std::move(retval)),
                .right = std::make_unique<OpList>(std::move(*lhs_command)),
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

/**
 * BNF:
 * 
 * ```
 * command ::= words
 *           | subshell
 *           ;
 * ```
 * 
 * @param tokenizer 
 * @return std::optional<Command> 
 */
std::optional<Command> SyntaxTree::command(Tokenizer &tokenizer) const
{
    if (auto syn_words = this->check<Command>(tokenizer, &SyntaxTree::words))
        return syn_words;

    if (auto syn_subshell = this->check<Command>(tokenizer, &SyntaxTree::subshell))
        return syn_subshell;

    return std::nullopt;
}

/**
 * BNF:
 *
 * ```
 * subshell ::= OPEN_ROUND complete_command CLOSE_ROUND
 *            ;
 * ```
 *
 * @param tokenizer
 * @return std::optional<Subshell>
 */
std::optional<Subshell> SyntaxTree::subshell(Tokenizer &tokenizer) const
{
    auto open_round = tokenizer.next_token();
    if (!open_round.has_value())
        return std::nullopt;

    if (open_round->type != TokenType::open_round)
        return std::nullopt;

    auto complete_command = this->complete_command(tokenizer);
    if (!complete_command.has_value())
        return std::nullopt;

    auto close_round = tokenizer.next_token();
    if (!close_round.has_value())
        return std::nullopt;

    if (close_round->type != TokenType::close_round)
        return std::nullopt;

    return Subshell{
        .seq_list = std::make_unique<SequentialList>(std::move(*complete_command)),
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

    if (prog_token->type != TokenType::word && prog_token->type != TokenType::number)
        return std::nullopt;

    std::vector<std::string_view> args{};

    while (const auto arg = tokenizer.peek())
    {
        if (arg->type != TokenType::word && arg->type != TokenType::number)
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