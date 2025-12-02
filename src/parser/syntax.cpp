#include "syntax.h"
#include <print>
#include <cstdlib>
#include <charconv>

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
 * program_substitution ::= ANDOPEN program CLOSE_ROUND
 *                        ;
 * ```
 * 
 * @param tokenizer 
 * @return std::optional<ThisProgram> 
 */
std::optional<ThisProgram> SyntaxTree::program_substitution(Tokenizer &tokenizer) const
{
    Tokenizer sub_tokenizer{tokenizer};

    const auto and_open = this->token(sub_tokenizer, TokenType::andopen);
    if (!and_open)
        return std::nullopt;

    auto program = this->program(sub_tokenizer);
    if (!program)
        return std::nullopt;

    const auto close_round = this->token(sub_tokenizer, TokenType::close_round);
    // TODO: error handling: print "missing closing paren"
    if (!close_round)
        return std::nullopt;

    tokenizer = sub_tokenizer;

    return program;
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
std::optional<ThisProgram> SyntaxTree::program(Tokenizer &tokenizer) const
{
    Tokenizer sub_tokenizer{tokenizer};

    this->linebreak(sub_tokenizer);

    if (sub_tokenizer.next_is_eof())
    {
        tokenizer = sub_tokenizer;
        return {};
    }

    auto complete_commands = this->complete_commands(sub_tokenizer);
    if (!complete_commands.has_value())
        return std::nullopt;

    this->linebreak(sub_tokenizer);

    tokenizer = sub_tokenizer;

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

        if (!this->newline_list(tokenizer))
            break;
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

    // Advance the tokenizer if the next token is a SEMI.
    // If the next token is a SEMI, the tokenizer will be automatically
    // advanced by the function.
    this->token(tokenizer, TokenType::semicolon);

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

    auto first_op_list = this->and_or(tokenizer);
    if (!first_op_list)
        return std::nullopt;

    retval.right = std::make_unique<OpList>(std::move(*first_op_list));

    // Check if there are other sequential list to concatenate to the return value
    for (;;)
    {
        // If the next token is not what we expect don't advance the tokenizer sequence
        Tokenizer sub_token{tokenizer};

        const auto semi = this->token(sub_token, TokenType::semicolon);
        if (!semi)
        {
            break;
        }

        auto next_op_list = this->and_or(sub_token);
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
 * and_or ::=                         pipeline
 *          | and_or AND_IF linebreak pipeline
 *          | and_or OR_IF  linebreak pipeline
 *          ;
 * ```
 *
 * @param tokenizer
 * @return std::optional<OpList>
 */
std::optional<OpList> SyntaxTree::and_or(Tokenizer &tokenizer) const
{
    auto pipeline = this->pipeline(tokenizer);
    if (!pipeline)
        return std::nullopt;

    OpList retval = std::move(*pipeline);

    for (;;)
    {
        // Duplicate the tokenizer. By duplicating, if the parsing
        // fails, the advancements are not committed to the original
        // tokenizer.
        Tokenizer sub_tok{tokenizer};

        auto next_token = sub_tok.next_token();
        if (!next_token)
        {
            break;
        }

        // After this if the token is either and_and or or_or
        if (next_token->type != TokenType::and_and && next_token->type != TokenType::or_or)
        {
            break;
        }

        this->linebreak(sub_tok);

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
 * pipeline ::=      pipe_sequence
 *            | Bang pipe_sequence
 *            ;
 * ```
 *
 * @param tokenizer
 * @return std::optional<Pipeline>
 */
std::optional<Pipeline> SyntaxTree::pipeline(Tokenizer &tokenizer) const
{
    bool has_bang = false;

    const auto bang = this->token(tokenizer, TokenType::bang);
    if (bang)
    {
        has_bang = true;
    }

    auto pipe_sequence = this->pipe_sequence(tokenizer);
    if (pipe_sequence)
    {
        pipe_sequence->negated = has_bang;
    }

    return pipe_sequence;
}

/**
 * BNF:
 *
 * ```
 * pipe_sequence ::=                             command
 *                 | pipe_sequence '|' linebreak command
 *                 ;
 * ```
 *
 * @param tokenizer
 * @return std::optional<Pipeline>
 */
std::optional<Pipeline> SyntaxTree::pipe_sequence(Tokenizer &tokenizer) const
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

        const auto pipe = this->token(sub_token, TokenType::pipe);
        if (!pipe)
            break;

        this->linebreak(sub_token);

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
 *           | compound_command
 *           | compound_command redirect_list
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

    Tokenizer subshell_tok{tokenizer};
    auto subshell = this->compound_command(subshell_tok);
    if (subshell)
    {
        // Add redirect list if present
        auto redirect_list = this->redirect_list(subshell_tok);
        if (redirect_list)
        {
            subshell->redirections = std::move(*redirect_list);
        }

        tokenizer = subshell_tok;

        return subshell;
    }

    return std::nullopt;
}

/**
 * BNF:
 *
 * ```
 * compound_command ::= brace_group
 *                    | subshell
 *                    | for_clause
 *                    | case_clause
 *                    | if_clause
 *                    | while_clause
 *                    | until_clause
 *                    ;
 * ```
 *
 * @param tokenizer
 * @return std::optional<Command>
 */
std::optional<Subshell> SyntaxTree::compound_command(Tokenizer &tokenizer) const
{
    // TODO: modify return value with appropriate variant
    return this->subshell(tokenizer);
}

/**
 * BNF:
 *
 * ```
 * subshell ::= '(' compound_list ')'
 *            ;
 * ```
 *
 * @param tokenizer
 * @return std::optional<Subshell>
 */
std::optional<Subshell> SyntaxTree::subshell(Tokenizer &tokenizer) const
{
    Tokenizer sub_tokenizer{tokenizer};

    const auto open_round = this->token(sub_tokenizer, TokenType::open_round);
    if (!open_round)
        return std::nullopt;

    // TODO: change this to compound_list
    auto compound_list = this->compound_list(sub_tokenizer);
    if (!compound_list)
        return std::nullopt;

    const auto close_round = this->token(sub_tokenizer, TokenType::close_round);
    if (!close_round)
        return std::nullopt;

    tokenizer = sub_tokenizer;

    return Subshell{
        .seq_list = std::make_unique<SequentialList>(std::move(*compound_list)),
    };
}

/**
 * BNF:
 * 
 * ```
 * compound_list ::= linebreak term
 *                 | linebreak term separator
 *                 ;
 * ```
 * 
 * @param tokenizer 
 * @return std::optional<SequentialList> 
 */
std::optional<SequentialList> SyntaxTree::compound_list(Tokenizer &tokenizer) const
{
    Tokenizer sub_tok{tokenizer};

    this->linebreak(sub_tok);

    auto term = this->term(sub_tok);
    if (!term)
        return std::nullopt;

    this->token(sub_tok, TokenType::semicolon);

    tokenizer = sub_tok;

    return term;
}

/**
 * BNF:
 * 
 * ```
 * term ::= term separator and_or
 *        |                and_or
 *        ;
 * ```
 * 
 * @param tokenizer 
 * @return std::optional<OpList> 
 */
std::optional<SequentialList> SyntaxTree::term(Tokenizer &tokenizer) const
{
    SequentialList retval{};

    auto and_or = this->and_or(tokenizer);
    if (!and_or)
        return std::nullopt;

    retval.right = std::make_unique<OpList>(std::move(*and_or));

    // Check if there are other sequential list to concatenate to the return value
    for (;;)
    {
        // If the next token is not what we expect don't advance the tokenizer sequence
        Tokenizer sub_token{tokenizer};

        const auto semi = this->token(sub_token, TokenType::semicolon);
        if (!semi)
        {
            break;
        }

        auto next_op_list = this->and_or(sub_token);
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

    std::vector<Token> args{};
    std::vector<Redirect> redirects{};

    while (!tokenizer.next_is_eof())
    {
        auto redirect = this->io_redirect(tokenizer);
        if (redirect)
        {
            redirects.emplace_back(*redirect);
            continue;
        }

        auto arg = this->word(tokenizer);
        if (arg)
        {
            args.emplace_back(*arg);
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
 * redirect_list ::=               io_redirect
 *                 | redirect_list io_redirect
 *                 ;
 * ```
 *
 * @param tokenizer
 * @return std::optional<std::vector<Redirect>>
 */
std::optional<std::vector<Redirect>> SyntaxTree::redirect_list(Tokenizer &tokenizer) const
{
    Tokenizer io_tokenizer{tokenizer};

    auto first_redirect = this->io_redirect(io_tokenizer);
    if (!first_redirect)
        return std::nullopt;

    std::vector redirects{std::move(*first_redirect)};
    // Commit the advancements to the tokenizer
    tokenizer = io_tokenizer;

    for (;;)
    {
        auto next_redirect = this->io_redirect(tokenizer);

        if (!next_redirect)
            break;

        redirects.emplace_back(std::move(*next_redirect));
    }

    return redirects;
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
    Tokenizer sub_tokenizer{tokenizer};
    std::optional<int> new_redirect_fd;

    const auto io_number = sub_tokenizer.peek();
    if (!io_number)
        return std::nullopt;

    if (io_number->type == TokenType::io_number)
    {
        const auto tmp_num = std::string(io_number->value);
        new_redirect_fd = std::atoi(tmp_num.c_str());

        // Commit advancement to the tokenizer
        sub_tokenizer.next_token();
    }

    auto redirect = this->io_file(sub_tokenizer);
    if (!redirect)
        return std::nullopt;

    // Replace fd if io_number is present
    if (new_redirect_fd)
    {
        std::visit(
            overloads{
                [&](FileRedirect &file)
                { file.redirect_fd = *new_redirect_fd; },
                [&](FdRedirect &fd)
                { fd.fd_to_replace = *new_redirect_fd; },
                [&](CloseFd &close)
                { close.fd = *new_redirect_fd; },
            },
            *redirect);
    }

    tokenizer = sub_tokenizer;

    return redirect;
}

static Redirect convert_and_redirect(const int default_fd, const std::string_view filename)
{
    assertm(!filename.empty(), "filename must not be empty, other wise std::from_chars will fail");

    Redirect redirect;

    int fd{};
    auto [ptr, ec] = std::from_chars(filename.data(), filename.data() + filename.size(), fd);

    if (ec == std::errc())
    {
        redirect = FdRedirect{.fd_to_replace = default_fd, .fd_replacer = fd};
    }
    else if (filename == "-")
    {
        redirect = CloseFd{.fd = default_fd};
    }
    else
    {
        // TODO: error handling
        throw std::runtime_error(std::format("Redirect is not a number or dash! fd={}", filename));
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
    Tokenizer sub_tokenizer{tokenizer};

    const auto redirect_token = sub_tokenizer.next_token();
    if (!redirect_token.has_value())
        return std::nullopt;

    // TODO: error handling
    auto filename = this->filename(sub_tokenizer);
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

    case TokenType::lessand:
        redirect = convert_and_redirect(STDIN_FILENO, *filename);
        break;

    case TokenType::greatand:
        redirect = convert_and_redirect(STDOUT_FILENO, *filename);
        break;

    default:
        return std::nullopt;
    }

    tokenizer = sub_tokenizer;

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
    const auto word = tokenizer.peek();
    if (!word.has_value())
        return std::nullopt;

    if (word->type != TokenType::word)
        return std::nullopt;

    tokenizer.next_token();

    return word->value;
}

/**
 * BNF:
 * 
 * ```
 * newline_list ::=              NEWLINE
 *                | newline_list NEWLINE
 *                ;
 * ```
 * 
 * @param tokenizer 
 * @return std::optional<std::string_view> 
 */
bool SyntaxTree::newline_list(Tokenizer &tokenizer) const
{
    const auto newline = tokenizer.peek();
    if (!newline)
        return false;

    if (newline->type != TokenType::new_line)
        return false;

    tokenizer.next_token();

    for (;;)
    {
        const auto next_newline = tokenizer.peek();
        if (!newline || next_newline->type != TokenType::new_line)
            break;

        tokenizer.next_token();
    }

    return true;
}

/**
 * BNF:
 * 
 * ```
 * linebreak ::= newline_list
 *             | EMPTY
 *             ;
 * ```
 * 
 * @param tokenizer 
 * @return true 
 * @return false 
 */
void SyntaxTree::linebreak(Tokenizer &tokenizer) const
{
    this->newline_list(tokenizer);
}

std::optional<Token> SyntaxTree::word(Tokenizer &tokenizer) const
{
    const auto word = tokenizer.peek();
    if (!word)
        return std::nullopt;

    if (word->type != TokenType::word)
        return std::nullopt;

    tokenizer.next_token();

    return word;
}

inline std::optional<Token> SyntaxTree::token(Tokenizer &tokenizer, const TokenType type) const
{
    auto token = tokenizer.peek();
    if (!token || token->type != type)
        return std::nullopt;

    tokenizer.next_token();
    return token;
}