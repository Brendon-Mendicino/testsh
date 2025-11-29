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

/**
 * BNF:
 *
 * ```
 * program ::= linebreak
 *           | linebreak complete_commands linebreak
 *           ;
 * ```
 *
 * @param tokenizer
 * @return std::optional<ThisProgram>
 */
std::optional<ThisProgram> SyntaxTree::build(Tokenizer &tokenizer)
{
    if (tokenizer.next_is_eof())
        return {};

    auto complete_commands = this->complete_commands(tokenizer);
    if (!complete_commands.has_value())
        return std::nullopt;

    return ThisProgram{
        .child = std::make_unique<CompleteCommands>(std::move(*complete_commands)),
    };
}

/**
 * BNF:
 *
 * ```
 * complete_commands ::= complete_command
 *                     | complete_commands newline_list complete_command
 *                     ;
 * ```
 *
 * @param tokenizer
 * @return std::optional<CompleteCommands>
 */
std::optional<CompleteCommands> SyntaxTree::complete_commands(Tokenizer &tokenizer) const
{
    CompleteCommands complete_commands;

    while (auto complete_command = this->complete_command(tokenizer))
    {
        complete_commands.emplace_back(std::move(*complete_command));
    }

    return complete_commands;
}

/**
 * BNF:
 *
 * ```
 * complete_command ::= list
 *                    | list SEMI
 *                    ;
 * ```
 *
 * @param tokenizer
 * @return std::optional<SequentialList>
 */
std::optional<SequentialList> SyntaxTree::complete_command(Tokenizer &tokenizer) const
{
    auto list = this->list(tokenizer);
    if (!list.has_value())
        return std::nullopt;

    const auto semi = tokenizer.peek();
    if (semi.has_value() && semi->type == TokenType::semicolon)
    {
        // Advance the tokenizer if the next token is a SEMI
        tokenizer.next_token();
    }

    return list;
}

/**
 * BNF:
 *
 * ```
 * list ::= op_list
 *        | list SEMI op_list
 *        ;
 * ```
 *
 * @param tokenizer
 * @return std::optional<SequentialList>
 */
std::optional<SequentialList> SyntaxTree::list(Tokenizer &tokenizer) const
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
 * op_list ::= pipeline
 *           | op_list AND_AND pipeline
 *           | op_list OR_OR pipeline
 *           ;
 * ```
 *
 * @param tokenizer
 * @return std::optional<OpList>
 */
std::optional<OpList> SyntaxTree::op_list(Tokenizer &tokenizer) const
{
    auto pipeline = this->pipeline(tokenizer);
    if (!pipeline.has_value())
    {
        return std::nullopt;
    }

    OpList retval = std::move(*pipeline);

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

        // The next tokens must be a pipeline
        // TODO: do some error handling?
        auto rhs_pipeline = this->pipeline(sub_tok);
        if (!rhs_pipeline.has_value())
        {
            break;
        }

        if (next_token->type == TokenType::and_and)
        {
            AndList and_and{
                .left = std::make_unique<OpList>(std::move(retval)),
                .right = std::make_unique<OpList>(std::move(*rhs_pipeline)),
            };

            retval = std::move(and_and);
        }
        else if (next_token->type == TokenType::or_or)
        {
            OrList or_or{
                .left = std::make_unique<OpList>(std::move(retval)),
                .right = std::make_unique<OpList>(std::move(*rhs_pipeline)),
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
 * pipeline ::= command
 *            | pipeline PIPE command
 *            ;
 * ```
 *
 * @param tokenizer
 * @return std::optional<Pipeline>
 */
std::optional<Pipeline> SyntaxTree::pipeline(Tokenizer &tokenizer) const
{
    Pipeline retval{};

    auto first_command = this->command(tokenizer);
    if (!first_command.has_value())
        return std::nullopt;

    retval.right = std::make_unique<Command>(std::move(*first_command));

    // Check if there are other commands to concatenate to the return value
    for (;;)
    {
        // If the next token is not what we expect don't advance the tokenizer sequence
        Tokenizer sub_token{tokenizer};

        const auto pipe = sub_token.next_token();
        if (!pipe.has_value())
        {
            break;
        }

        if (pipe->type != TokenType::pipe)
        {
            break;
        }

        auto next_command = this->command(sub_token);
        if (!next_command.has_value())
        {
            // TODO: do some error handling
            break;
        }

        Pipeline new_pipeline{
            .left = std::make_unique<Pipeline>(std::move(retval)),
            .right = std::make_unique<Command>(std::move(*next_command)),
        };

        retval = std::move(new_pipeline);

        tokenizer = sub_token;
    }

    return retval;
}

/**
 * BNF:
 *
 * ```
 * command ::= simple_command
 *           | subshell
 *           ;
 * ```
 *
 * @param tokenizer
 * @return std::optional<Command>
 */
std::optional<Command> SyntaxTree::command(Tokenizer &tokenizer) const
{
    if (auto syn_words = this->check<Command>(tokenizer, &SyntaxTree::simple_command))
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

/**
 * BNF:
 *
 * ```
 * simple_command ::= cmd_prefix cmd_word cmd_suffix
 *                  | cmd_prefix cmd_word
 *                  | cmd_prefix
 *                  | cmd_name cmd_suffix
 *                  | cmd_name
 *                  ;
 * cmd_name       ::= WORD                       Apply rule 7a
 *                  ;
 * cmd_word       ::= WORD                       Apply rule 7b
 *                  ;
 * cmd_prefix     ::=            io_redirect
 *                  | cmd_prefix io_redirect
 *                  |            ASSIGNMENT_WORD
 *                  | cmd_prefix ASSIGNMENT_WORD
 *                  ;
 * cmd_suffix     ::=            io_redirect
 *                  | cmd_suffix io_redirect
 *                  |            WORD
 *                  | cmd_suffix WORD
 *                  ;
 * ```
 *
 * @param tokenizer
 * @return std::optional<SimpleCommand>
 */
std::optional<SimpleCommand> SyntaxTree::simple_command(Tokenizer &tokenizer) const
{
    const auto cmd_word = this->word(tokenizer);
    if (!cmd_word)
        return std::nullopt;

    std::vector<std::string_view> args{};
    std::vector<Redirect> redirects{};

    while (!tokenizer.next_is_eof())
    {
        Tokenizer word_tok{tokenizer};
        const auto arg = this->word(word_tok);
        if (arg)
        {
            tokenizer = word_tok;
            args.emplace_back(*arg);
            continue;
        }

        Tokenizer redirect_tok{tokenizer};
        auto redirect = this->io_redirect(redirect_tok);
        if (redirect)
        {
            tokenizer = redirect_tok;
            redirects.emplace_back(std::move(*redirect));
            continue;
        }

        break;
    }

    return SimpleCommand{
        .program = *cmd_word,
        .arguments = std::move(args),
        .redirections = std::move(redirects),
    };
}

/**
 * BNF:
 *
 * ```
 * io_redirect ::=           io_file
 *               | IO_NUMBER io_file
 *               |           io_here
 *               | IO_NUMBER io_here
 *               ;
 * ```
 *
 * @param tokenizer
 * @return std::optional<Redirect>
 */
std::optional<Redirect> SyntaxTree::io_redirect(Tokenizer &tokenizer) const
{
    std::optional<int> new_redirect_fd;

    const auto number_token = tokenizer.peek();
    if (!number_token.has_value())
        return std::nullopt;

    if (number_token->type == TokenType::number)
    {
        const auto tmp_num = std::string(number_token->value);
        new_redirect_fd = std::atoi(tmp_num.c_str());
    }

    auto redirect = this->io_file(tokenizer);

    // Replace fd
    if (new_redirect_fd.has_value() && redirect.has_value())
    {
        std::visit(
            overloads{
                [&](FileRedirect &file)
                { file.redirect_fd = *new_redirect_fd; },
                [&](FdRedirect &fd) {},
            },
            *redirect);
    }

    return redirect;
}

/**
 * BNF:
 *
 * ```
 * io_file ::= '<'       filename
 *           | LESSAND   filename
 *           | '>'       filename
 *           | GREATAND  filename
 *           | DGREAT    filename
 *           | LESSGREAT filename
 *           | CLOBBER   filename
 *           ;
 * ```
 *
 * @param tokenizer
 * @return std::optional<Redirect>
 */
std::optional<Redirect> SyntaxTree::io_file(Tokenizer &tokenizer) const
{
    const auto redirect_token = tokenizer.next_token();
    if (!redirect_token.has_value())
        return std::nullopt;

    // TODO: error handling
    auto filename = this->filename(tokenizer);
    if (!filename.has_value())
        return std::nullopt;

    Redirect redirect;

    switch (redirect_token->type)
    {
    case TokenType::less:
        redirect = FileRedirect{.redirect_fd = STDIN_FILENO, .file_kind = OpenKind::read, .filename = std::move(*filename)};
        break;

    case TokenType::great:
        redirect = FileRedirect{.redirect_fd = STDOUT_FILENO, .file_kind = OpenKind::replace, .filename = std::move(*filename)};
        break;

    case TokenType::dgreat:
        redirect = FileRedirect{.redirect_fd = STDOUT_FILENO, .file_kind = OpenKind::append, .filename = std::move(*filename)};
        break;

    case TokenType::lessgreat:
        redirect = FileRedirect{.redirect_fd = STDIN_FILENO, .file_kind = OpenKind::rw, .filename = std::move(*filename)};
        break;

    default:
        return std::nullopt;
    }

    return redirect;
}

std::optional<Redirect> SyntaxTree::io_here(Tokenizer &tokenizer) const
{
    throw std::runtime_error("Not implemented");
}

/**
 * BNF:
 *
 * ```
 * filename ::= WORD
 *            ;
 * ```
 *
 * @param tokenizer
 * @return std::optional<std::string>
 */
std::optional<std::string_view> SyntaxTree::filename(Tokenizer &tokenizer) const
{
    const auto word = tokenizer.next_token();
    if (!word.has_value())
        return std::nullopt;

    const auto type = word->type;
    if (type != TokenType::word && type != TokenType::number)
        return std::nullopt;

    return word->value;
}

std::optional<std::string_view> SyntaxTree::word(Tokenizer &tokenizer) const
{
    const auto word = tokenizer.next_token();
    if (!word.has_value())
        return std::nullopt;

    if (word->type != TokenType::word && word->type != TokenType::number)
        return std::nullopt;

    return word->value;
}
