#include "syntax.h"
#include "tokenizer.h"
#include "util.h"
#include <charconv>
#include <cstdlib>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>

// ------------------------------------
// SimpleCommand
// ------------------------------------

std::string SimpleCommand::text() const {
    std::string cmd{this->program};

    for (const auto &arg : this->arguments) {
        cmd += " " + arg;
    }

    return cmd;
}

// ------------------------------------
// SequentialList
// ------------------------------------

SequentialList SequentialList::from_async(AsyncList &&async) {
    if (!async.left)
        return SequentialList{.right = std::move(async.right)};

    return SequentialList{
        .left = std::move(*async.left),
        .right = std::move(async.right),
    };
}

// ------------------------------------
// AsyncList
// ------------------------------------

AsyncList AsyncList::from_seq(SequentialList &&seq) {
    if (!seq.left)
        return AsyncList{.right = std::move(seq.right)};

    return AsyncList{
        .left = std::move(*seq.left),
        .right = std::move(seq.right),
    };
}

// ------------------------------------
// SyntaxTree
// ------------------------------------

/**
 * BNF:
 *
 * ```
 * cmd_substitution ::= ANDOPEN compound_list CLOSE_ROUND
 *                    ;
 * ```
 *
 * @param tokenizer
 * @return std::optional<ThisProgram>
 */
template <IsTokenizer Tok>
std::optional<CmdSub> SyntaxTree<Tok>::cmdsub(Tok &tokenizer) const {
    Tok sub_tok{tokenizer};

    const auto open_open = this->token(sub_tok, TokenType::andopen);
    if (!open_open)
        return std::nullopt;

    auto compound_list = this->compound_list(sub_tok);
    if (!compound_list)
        return std::nullopt;

    const auto close_round = this->token(sub_tok, TokenType::close_round);
    if (!close_round)
        return std::nullopt;

    tokenizer = sub_tok;

    return CmdSub{
        .seq_list = std::make_unique<List>(take(compound_list)),
    };
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
template <IsTokenizer Tok>
std::optional<ThisProgram> SyntaxTree<Tok>::program(Tok &tokenizer) const {
    Tok sub_tokenizer{tokenizer};

    this->linebreak(sub_tokenizer);

    if (sub_tokenizer.next_is_eof()) {
        tokenizer = sub_tokenizer;
        return ThisProgram{};
    }

    auto complete_commands = this->complete_commands(sub_tokenizer);
    if (!complete_commands || complete_commands->empty())
        return std::nullopt;

    this->linebreak(sub_tokenizer);

    tokenizer = sub_tokenizer;

    return ThisProgram{
        .child = std::move(*complete_commands),
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
template <IsTokenizer Tok>
std::optional<CompleteCommands>
SyntaxTree<Tok>::complete_commands(Tok &tokenizer) const {
    CompleteCommands complete_commands;

    while (auto complete_command = this->complete_command(tokenizer)) {
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
 *                    | list separator_op
 *                    ;
 * ```
 *
 * @param tokenizer
 * @return std::optional<SequentialList>
 */
template <IsTokenizer Tok>
std::optional<List> SyntaxTree<Tok>::complete_command(Tok &tokenizer) const {
    auto list = this->list(tokenizer);
    if (!list.has_value())
        return std::nullopt;

    // Advance the tokenizer if the next token is a SEMI.
    // If the next token is a SEMI, the tokenizer will be automatically
    // advanced by the function.
    const auto sep = this->separator_op(tokenizer);
    if (sep && sep->type == TokenType::andper && list) {
        return AsyncList::from_seq(std::move(*list));
    }

    return list;
}

/**
 * BNF:
 *
 * ```
 * list ::=                and_or
 *        | list separator and_or
 *        ;
 * ```
 *
 * @param tokenizer
 * @return std::optional<SequentialList>
 */
template <IsTokenizer Tok>
std::optional<SequentialList> SyntaxTree<Tok>::list(Tok &tokenizer) const {
    SequentialList retval{};

    auto first_op_list = this->and_or(tokenizer);
    if (!first_op_list)
        return std::nullopt;

    retval.right = std::make_unique<OpList>(std::move(*first_op_list));

    // Check if there are other sequential list to concatenate to the return
    // value
    for (;;) {
        // If the next token is not what we expect don't advance the tokenizer
        // sequence
        Tok sub_token{tokenizer};

        const auto sep = this->separator_op(sub_token);
        if (!sep) {
            break;
        }

        auto next_op_list = this->and_or(sub_token);
        if (!next_op_list.has_value()) {
            break;
        }

        SequentialList rotated_list{};

        if (sep->type == TokenType::andper) {
            // Convert the previous SequentialList to an AsyncList
            rotated_list = SequentialList{
                .left = std::make_unique<List>(
                    AsyncList::from_seq(std::move(retval))),
                .right = std::make_unique<OpList>(std::move(*next_op_list)),
            };
        } else {
            rotated_list = SequentialList{
                .left = std::make_unique<List>(std::move(retval)),
                .right = std::make_unique<OpList>(std::move(*next_op_list)),
            };
        }

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
template <IsTokenizer Tok>
std::optional<OpList> SyntaxTree<Tok>::and_or(Tok &tokenizer) const {
    auto pipeline = this->pipeline(tokenizer);
    if (!pipeline)
        return std::nullopt;

    OpList retval = std::move(*pipeline);

    for (;;) {
        // Duplicate the tokenizer. By duplicating, if the parsing
        // fails, the advancements are not committed to the original
        // tokenizer.
        Tok sub_tok{tokenizer};

        auto next_token = sub_tok.next_token();
        if (!next_token) {
            break;
        }

        // After this if the token is either and_and or or_or
        if (next_token->type != TokenType::and_and &&
            next_token->type != TokenType::or_or) {
            break;
        }

        this->linebreak(sub_tok);

        // The next tokens must be a pipeline
        // TODO: do some error handling?
        auto rhs_pipeline = this->pipeline(sub_tok);
        if (!rhs_pipeline.has_value()) {
            break;
        }

        if (next_token->type == TokenType::and_and) {
            AndList and_and{
                .left = std::make_unique<OpList>(std::move(retval)),
                .right = std::make_unique<OpList>(std::move(*rhs_pipeline)),
            };

            retval = std::move(and_and);
        } else if (next_token->type == TokenType::or_or) {
            OrList or_or{
                .left = std::make_unique<OpList>(std::move(retval)),
                .right = std::make_unique<OpList>(std::move(*rhs_pipeline)),
            };

            retval = std::move(or_or);
        } else {
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
template <IsTokenizer Tok>
std::optional<Pipeline> SyntaxTree<Tok>::pipeline(Tok &tokenizer) const {
    bool has_bang = false;

    const auto bang = this->token(tokenizer, TokenType::bang);
    if (bang) {
        has_bang = true;
    }

    auto pipe_sequence = this->pipe_sequence(tokenizer);
    if (pipe_sequence) {
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
template <IsTokenizer Tok>
std::optional<Pipeline> SyntaxTree<Tok>::pipe_sequence(Tok &tokenizer) const {
    Pipeline retval{};

    auto first_command = this->command(tokenizer);
    if (!first_command.has_value())
        return std::nullopt;

    retval.cmds.emplace_back(take(first_command));

    // Check if there are other commands to concatenate to the return value
    for (;;) {
        // If the next token is not what we expect don't advance the tokenizer
        // sequence
        Tok sub_token{tokenizer};

        const auto pipe = this->token(sub_token, TokenType::pipe);
        if (!pipe)
            break;

        this->linebreak(sub_token);

        auto next_command = this->command(sub_token);
        if (!next_command.has_value()) {
            // TODO: do some error handling
            break;
        }

        retval.cmds.emplace_back(take(next_command));

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
template <IsTokenizer Tok>
std::optional<Command> SyntaxTree<Tok>::command(Tok &tokenizer) const {
    auto simple_command = this->simple_command(tokenizer);
    if (simple_command) {
        return std::move(*simple_command);
    }

    Tok subshell_tok{tokenizer};
    auto subshell = this->compound_command(subshell_tok);
    if (subshell) {
        // Add redirect list if present
        auto redirect_list = this->redirect_list(subshell_tok);
        if (redirect_list) {
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
template <IsTokenizer Tok>
std::optional<Subshell>
SyntaxTree<Tok>::compound_command(Tok &tokenizer) const {
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
template <IsTokenizer Tok>
std::optional<Subshell> SyntaxTree<Tok>::subshell(Tok &tokenizer) const {
    Tok sub_tokenizer{tokenizer};

    const auto open_round = this->token(sub_tokenizer, TokenType::open_round);
    if (!open_round)
        return std::nullopt;

    auto compound_list = this->compound_list(sub_tokenizer);
    if (!compound_list)
        return std::nullopt;

    const auto close_round = this->token(sub_tokenizer, TokenType::close_round);
    if (!close_round)
        return std::nullopt;

    tokenizer = sub_tokenizer;

    return Subshell{
        .seq_list = std::make_unique<List>(std::move(*compound_list)),
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
template <IsTokenizer Tok>
std::optional<List> SyntaxTree<Tok>::compound_list(Tok &tokenizer) const {
    Tok sub_tok{tokenizer};

    this->linebreak(sub_tok);

    auto term = this->term(sub_tok);
    if (!term)
        return std::nullopt;

    const auto sep = this->separator(sub_tok);
    if (sep && sep->type == TokenType::andper) {
        tokenizer = sub_tok;

        return AsyncList::from_seq(std::move(*term));
    }

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
template <IsTokenizer Tok>
std::optional<SequentialList> SyntaxTree<Tok>::term(Tok &tokenizer) const {
    SequentialList retval{};

    auto and_or = this->and_or(tokenizer);
    if (!and_or)
        return std::nullopt;

    retval.right = std::make_unique<OpList>(std::move(*and_or));

    // Check if there are other sequential list to concatenate to the return
    // value
    for (;;) {
        // If the next token is not what we expect don't advance the tokenizer
        // sequence
        Tok sub_token{tokenizer};

        const auto sep = this->separator(sub_token);
        if (!sep) {
            break;
        }

        auto next_op_list = this->and_or(sub_token);
        if (!next_op_list.has_value()) {
            break;
        }

        SequentialList rotated_list{};

        if (sep->type == TokenType::andper) {
            // Convert the previous SequentialList to an AsyncList
            rotated_list = SequentialList{
                .left = std::make_unique<List>(
                    AsyncList::from_seq(std::move(retval))),
                .right = std::make_unique<OpList>(std::move(*next_op_list)),
            };
        } else {
            rotated_list = SequentialList{
                .left = std::make_unique<List>(std::move(retval)),
                .right = std::make_unique<OpList>(std::move(*next_op_list)),
            };
        }

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
 * ```
 *
 * @param tokenizer
 * @return std::optional<SimpleCommand>
 */
template <IsTokenizer Tok>
std::optional<Command> SyntaxTree<Tok>::simple_command(Tok &tokenizer) const {
    auto cmd_prefix = this->cmd_prefix(tokenizer);
    if (!cmd_prefix.empty()) {
        auto cmd_word = this->cmd_word(tokenizer);
        auto cmd_suffix = this->cmd_suffix(tokenizer);

        std::vector<Word> args{};
        std::vector<Redirect> redirects{};
        std::vector<AssignmentWord> assigments{};

        for (auto &s : cmd_prefix) {
            if (std::holds_alternative<AssignmentWord>(s))
                assigments.emplace_back(std::move(std::get<AssignmentWord>(s)));
            else
                redirects.emplace_back(std::move(std::get<Redirect>(s)));
        }

        for (auto &s : cmd_suffix) {
            if (std::holds_alternative<Word>(s))
                args.emplace_back(std::move(std::get<Word>(s)));
            else
                redirects.emplace_back(std::move(std::get<Redirect>(s)));
        }

        if (!cmd_word) {
            return SimpleAssignment{
                .redirections = redirects,
                .envs = assigments,
            };
        }

        return UnsubCommand{
            .program = std::make_unique<Word>(take(cmd_word)),
            .arguments = std::move(args),
            .redirections = std::move(redirects),
            .envs = std::move(assigments),
        };
    }

    auto cmd_name = this->cmd_name(tokenizer);
    if (cmd_name) {
        auto cmd_suffix = this->cmd_suffix(tokenizer);

        std::vector<Word> args{};
        std::vector<Redirect> redirects{};

        for (auto &s : cmd_suffix) {
            if (std::holds_alternative<Word>(s))
                args.emplace_back(std::move(std::get<Word>(s)));
            else
                redirects.emplace_back(std::move(std::get<Redirect>(s)));
        }

        return UnsubCommand{
            .program = std::make_unique<Word>(take(cmd_name)),
            .arguments = std::move(args),
            .redirections = std::move(redirects),
            .envs = {},
        };
    }

    return std::nullopt;
}

/**
 * BNF:
 *
 * ```
 * cmd_name       ::= WORD                       Apply rule 7a
 *                  ;
 * ```
 */
template <IsTokenizer Tok>
std::optional<Word> SyntaxTree<Tok>::cmd_name(Tok &tokenizer) const {
    return this->word(tokenizer);
}

/**
 * BNF:
 *
 * ```
 * cmd_word       ::= WORD                       Apply rule 7b
 *                  ;
 * ```
 */
template <IsTokenizer Tok>
std::optional<Word> SyntaxTree<Tok>::cmd_word(Tok &tokenizer) const {
    //     auto word = tokenizer.peek();
    //
    //     if (!word)
    //         return std::nullopt;
    //
    //     if (word->type != TokenType::word && word->type !=
    //     TokenType::quoted_word)
    //         return std::nullopt;
    //
    //     if (word->type == TokenType::word) {
    //         auto text = word->value;
    //         if (text.find("=") == 0)
    //             return std::nullopt;
    //     }
    //
    //     tokenizer.next_token();
    //
    //     return word;
    return this->word(tokenizer);
}

/**
 * BNF:
 *
 * ```
 * cmd_prefix     ::=            io_redirect
 *                  | cmd_prefix io_redirect
 *                  |            ASSIGNMENT_WORD
 *                  | cmd_prefix ASSIGNMENT_WORD
 *                  ;
 * ```
 */
template <IsTokenizer Tok>
std::vector<std::variant<AssignmentWord, Redirect>>
SyntaxTree<Tok>::cmd_prefix(Tok &tokenizer) const {
    std::vector<std::variant<AssignmentWord, Redirect>> retval{};

    for (;;) {
        if (auto word = this->assignment_word(tokenizer)) {
            retval.emplace_back(std::move(*word));
            continue;
        }

        if (auto redirect = this->io_redirect(tokenizer)) {
            retval.emplace_back(std::move(*redirect));
            continue;
        }

        break;
    }

    return retval;
}

/**
 * BNF:
 *
 * ```
 * cmd_suffix     ::=            io_redirect
 *                  | cmd_suffix io_redirect
 *                  |            WORD
 *                  | cmd_suffix WORD
 *                  ;
 * ```
 */
template <IsTokenizer Tok>
std::vector<std::variant<Word, Redirect>>
SyntaxTree<Tok>::cmd_suffix(Tok &tokenizer) const {
    std::vector<std::variant<Word, Redirect>> retval{};

    for (;;) {
        if (auto word = this->word(tokenizer)) {
            retval.emplace_back(take(word));
            continue;
        }

        if (auto redirect = this->io_redirect(tokenizer)) {
            retval.emplace_back(take(redirect));
            continue;
        }

        break;
    }

    return retval;
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
template <IsTokenizer Tok>
std::optional<std::vector<Redirect>>
SyntaxTree<Tok>::redirect_list(Tok &tokenizer) const {
    Tok io_tokenizer{tokenizer};

    auto first_redirect = this->io_redirect(io_tokenizer);
    if (!first_redirect)
        return std::nullopt;

    std::vector redirects{std::move(*first_redirect)};
    // Commit the advancements to the tokenizer
    tokenizer = io_tokenizer;

    for (;;) {
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
template <IsTokenizer Tok>
std::optional<Redirect> SyntaxTree<Tok>::io_redirect(Tok &tokenizer) const {
    Tok sub_tokenizer{tokenizer};
    std::optional<int> new_redirect_fd;

    const auto io_number = sub_tokenizer.peek();
    if (!io_number)
        return std::nullopt;

    if (io_number->type == TokenType::io_number) {
        const auto tmp_num = std::string(io_number->value);
        new_redirect_fd = std::atoi(tmp_num.c_str());

        // Commit advancement to the tokenizer
        sub_tokenizer.next_token();
    }

    auto redirect = this->io_file(sub_tokenizer);
    if (!redirect)
        return std::nullopt;

    // Replace fd if io_number is present
    if (new_redirect_fd) {
        std::visit(
            overloads{
                [&](FileRedirect &file) {
                    file.redirect_fd = *new_redirect_fd;
                },
                [&](FdRedirect &fd) { fd.fd_to_replace = *new_redirect_fd; },
                [&](CloseFd &close) { close.fd = *new_redirect_fd; },
            },
            *redirect);
    }

    tokenizer = sub_tokenizer;

    return redirect;
}

static Redirect convert_and_redirect(const int default_fd,
                                     const std::string_view filename) {
    assertm(!filename.empty(),
            "filename must not be empty, other wise std::from_chars will fail");

    Redirect redirect;

    int fd{};
    auto [ptr, ec] =
        std::from_chars(filename.data(), filename.data() + filename.size(), fd);

    if (ec == std::errc()) {
        redirect = FdRedirect{.fd_to_replace = default_fd, .fd_replacer = fd};
    } else if (filename == "-") {
        redirect = CloseFd{.fd = default_fd};
    } else {
        // TODO: error handling
        throw std::runtime_error(
            std::format("Redirect is not a number or dash! fd={}", filename));
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
template <IsTokenizer Tok>
std::optional<Redirect> SyntaxTree<Tok>::io_file(Tok &tokenizer) const {
    Tok sub_tokenizer{tokenizer};

    const auto redirect_token = sub_tokenizer.next_token();
    if (!redirect_token.has_value())
        return std::nullopt;

    // TODO: error handling
    auto filename = this->filename(sub_tokenizer);
    if (!filename.has_value())
        return std::nullopt;

    Redirect redirect;

    switch (redirect_token->type) {
    case TokenType::less:
        redirect = FileRedirect{.redirect_fd = STDIN_FILENO,
                                .file_kind = OpenKind::read,
                                .filename = std::move(*filename)};
        break;

    case TokenType::great:
        redirect = FileRedirect{.redirect_fd = STDOUT_FILENO,
                                .file_kind = OpenKind::replace,
                                .filename = std::move(*filename)};
        break;

    case TokenType::dgreat:
        redirect = FileRedirect{.redirect_fd = STDOUT_FILENO,
                                .file_kind = OpenKind::append,
                                .filename = std::move(*filename)};
        break;

    case TokenType::lessgreat:
        redirect = FileRedirect{.redirect_fd = STDIN_FILENO,
                                .file_kind = OpenKind::rw,
                                .filename = std::move(*filename)};
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

template <IsTokenizer Tok>
std::optional<Redirect> SyntaxTree<Tok>::io_here(Tok &tokenizer) const {
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
template <IsTokenizer Tok>
std::optional<std::string_view>
SyntaxTree<Tok>::filename(Tok &tokenizer) const {
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
template <IsTokenizer Tok>
bool SyntaxTree<Tok>::newline_list(Tok &tokenizer) const {
    const auto newline = tokenizer.peek();
    if (!newline)
        return false;

    if (newline->type != TokenType::new_line)
        return false;

    tokenizer.next_token();

    for (;;) {
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
template <IsTokenizer Tok>
void SyntaxTree<Tok>::linebreak(Tok &tokenizer) const {
    this->newline_list(tokenizer);
}

/**
 * BNF:
 *
 * ```
 * separator_op ::= '&'
 *                | ';'
 *                ;
 * ```
 *
 * @tparam Tok
 * @param tokenizer
 * @return std::optional<Token>
 */
template <IsTokenizer Tok>
std::optional<Token> SyntaxTree<Tok>::separator_op(Tok &tokenizer) const {
    const auto andp = this->token(tokenizer, TokenType::andper);
    if (andp)
        return andp;

    const auto semi = this->token(tokenizer, TokenType::semicolon);
    if (semi)
        return semi;

    return std::nullopt;
}

/**
 * BNF:
 *
 * ```
 * separator ::= separator_op linebreak
 *             | newline_list
 *             ;
 * ```
 *
 * @tparam Tok
 * @param tokenizer
 * @return std::optional<Token>
 */
template <IsTokenizer Tok>
std::optional<Token> SyntaxTree<Tok>::separator(Tok &tokenizer) const {
    const auto sep = this->separator_op(tokenizer);
    if (sep) {
        this->linebreak(tokenizer);
        return sep;
    }

    const auto newlines = this->newline_list(tokenizer);
    if (newlines) {
        return Token{.type = TokenType::new_line};
    }

    return std::nullopt;
}

template <IsTokenizer Tok>
std::optional<Word> SyntaxTree<Tok>::word(Tok &tokenizer) const {
    if (auto word = this->token(tokenizer, TokenType::word))
        return word;

    if (auto quoted = this->token(tokenizer, TokenType::quoted_word))
        return quoted;

    if (auto cmd_sub = this->cmdsub(tokenizer))
        return cmd_sub;

    return std::nullopt;
}

template <IsTokenizer Tok>
std::optional<AssignmentWord>
SyntaxTree<Tok>::assignment_word(Tok &tokenizer) const {
    Tok sub_tok{tokenizer};
    const std::string_view eq{"="};

    const auto word = this->token(sub_tok, TokenType::word);
    if (!word)
        return std::nullopt;

    const auto eq_pos = word->value.find(eq);

    if (eq_pos == 0 || eq_pos == std::string::npos)
        return std::nullopt;

    std::string_view key = word->value.substr(0, eq_pos);
    std::string_view value = word->value.substr(eq_pos + eq.size());

    tokenizer = sub_tok;

    return AssignmentWord{
        .whole = *word,
        .key = key,
        .value = value,
    };
}

template <IsTokenizer Tok>
inline std::optional<Token> SyntaxTree<Tok>::token(Tok &tokenizer,
                                                   const TokenType type) const {
    auto token = tokenizer.peek();
    if (!token || token->type != type)
        return std::nullopt;

    tokenizer.next_token();
    return token;
}

// ------------------------------------
// TEMPLATE INSTATIATION
// ------------------------------------

template class SyntaxTree<Tokenizer>;
template class SyntaxTree<UnbufferedTokenizer>;
template class SyntaxTree<TokenIter>;
