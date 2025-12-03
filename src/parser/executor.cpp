#include "executor.h"
#include "../util.h"
#include "../builtin/builtin.h"
#include <iostream>
#include <string>
#include <variant>
#include <memory>
#include <unistd.h>
#include <fcntl.h>
#include <wait.h>
#include <print>
#include <cerrno>
#include <stdexcept>
#include <cstring>
#include <algorithm>
#include <ranges>

namespace vw = std::ranges::views;

static bool fd_is_valid(int fd)
{
    return fcntl(fd, F_GETFD) != -1 || errno != EBADF;
}

/**
 * This class destructor automatically calls close() on all the
 * open file descriptors that the parent doesn't need to handle.
 *
 */
class RedirectController
{
    // tuple<to_replace, replacer>
    std::vector<std::tuple<int, int>> file_redirects;
    // tuple<to_replace, replacer>
    std::vector<std::tuple<int, int>> duplications;
    std::vector<int> fd_to_close;

public:
    RedirectController(const CommandState &state) : file_redirects(state.redirects), duplications(), fd_to_close(state.fd_to_close) {}

    RedirectController(const RedirectController &f) = delete;
    RedirectController(RedirectController &&f) = delete;
    RedirectController &operator=(const RedirectController &f) = delete;
    RedirectController &operator=(RedirectController &&f) = delete;

    ~RedirectController()
    {
        // When the object is destructed only close the fds that are originated
        // from a file. The duplications<> must not be touched by the parent,
        // redirections are only need for the child. For this reason
        // the duplicatoion fds on the parent must remain intact.
        for (const auto [_, replacer] : this->file_redirects)
        {
            close(replacer);
        }
    }

    bool add_redirects(const std::vector<Redirect> &redirections)
    {
        for (const auto &redirect : redirections)
        {
            bool success = std::visit(
                overloads{
                    [&](const FileRedirect &file_r)
                    {
                        int flags{};

                        switch (file_r.file_kind)
                        {
                        case OpenKind::read:
                            flags = O_RDONLY;
                            break;

                        case OpenKind::replace:
                            flags = O_CREAT | O_TRUNC | O_WRONLY;
                            break;

                        case OpenKind::append:
                            flags = O_CREAT | O_APPEND | O_WRONLY;
                            break;

                        case OpenKind::rw:
                            flags = O_CREAT | O_RDWR;
                            break;
                        }

                        std::string tmp_filename{file_r.filename};
                        mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH;
                        int open_fd = open(tmp_filename.c_str(), flags, mode);

                        if (open_fd == -1)
                        {
                            std::println(stderr, "open: {}", std::strerror(errno));
                            return false;
                        }

                        this->file_redirects.emplace_back(file_r.redirect_fd, open_fd);
                        return true;
                    },
                    [&](const FdRedirect &dup_fd)
                    {
                        // Check if the replacer fd exists
                        if (!fd_is_valid(dup_fd.fd_replacer))
                        {
                            std::println(stderr, "testsh: file descriptor {} does not exist", dup_fd.fd_replacer);
                            return false;
                        }

                        this->duplications.emplace_back(dup_fd.fd_to_replace, dup_fd.fd_replacer);
                        return true;
                    },
                    [&](const CloseFd &close_fd)
                    {
                        this->fd_to_close.emplace_back(close_fd.fd);
                        return true;
                    },
                },
                redirect);

            if (!success)
                return false;
        }

        return true;
    }

    bool apply_redirections() const
    {
        // close fds: leftover from the pipe() calls
        for (const auto to_close : this->fd_to_close)
        {
            close(to_close);
        }

        // Duplicate fds from files
        for (const auto [to_replace, replacer] : this->file_redirects)
        {
            const int retval = dup2(replacer, to_replace);
            if (retval == -1)
            {
                std::println(stderr, "dup2: {}", std::strerror(errno));
                return false;
            }
        }

        // Duplicate fds from duplication syntax
        for (const auto [to_replace, replacer] : this->duplications)
        {
            const int retval = dup2(replacer, to_replace);
            if (retval == -1)
            {
                std::println(stderr, "dup2: {}", std::strerror(errno));
                return false;
            }
        }

        return true;
    }
};

/**
 * @brief Create a pipe object
 *
 * @return std::optional<std::tuple<int, int>> tuple{reader_fd, writer_fd};
 */
static std::optional<std::tuple<int, int>> create_pipe()
{
    int pipefd[2] = {-1, -1};
    const int retval = pipe(pipefd);

    if (retval == -1)
    {
        std::println(stderr, "error: pipe: {}", std::strerror(errno));
        return std::nullopt;
    }

    return std::tuple{pipefd[0], pipefd[1]};
}

std::optional<ExecStats> Executor::builtin(const SimpleCommand &cmd) const
{
    if (cmd.program.text() == "cd")
    {
        const int exit_code = builtin_cd(cmd);
        return ExecStats{
            .exit_code = exit_code,
        };
    }

    else if (cmd.program.text() == "exec")
    {
        const int exit_code = builtin_exec(cmd);
        return ExecStats{
            .exit_code = exit_code,
        };
    }

    else if (cmd.program.text() == "exit")
    {
        const int exit_code = builtin_exit(cmd);
        return ExecStats{
            .exit_code = exit_code,
        };
    }

    return std::nullopt;
}

ExecStats Executor::simple_command(const SimpleCommand &cmd, const CommandState &state) const
{
    // Close the redirections used by the child, the parent no longer needs
    // them. The unneeded files will be automatically closed when the
    // destructor will be called.
    RedirectController redirect{state};

    if (!redirect.add_redirects(cmd.redirections))
    {
        return {.exit_code = 1};
    }

    // Check if a builtin can be run first before, before running
    // the program through exec().
    const auto builtin_run = this->builtin(cmd);
    if (builtin_run)
        return *builtin_run;

    const pid_t pid = fork();
    if (pid == -1)
    {
        throw std::runtime_error(std::string("fork failed: ") + std::strerror(errno));
    }

    if (pid == 0)
    {
        // -----------
        // Child
        // -----------

        if (!redirect.apply_redirections())
            exit(1);

        const ArgsToExec exec{cmd};
        const auto args_to_pass = exec.args()->get();

        execvp(args_to_pass[0], (char *const *)args_to_pass);
        exit(1);
    }

    // -----------
    // Parent
    // -----------

    if (state.inside_pipeline)
    {
        // TODO: mofiy this logic
        // Don't wait for the child
        return {.child_pid = pid};
    }

    int child_status;
    pid_t child_wait = waitpid(pid, &child_status, 0);
    if (child_wait != pid)
        throw std::runtime_error(std::format("The wait returened an error! retval={}", child_wait));

    return ExecStats{
        .exit_code = WEXITSTATUS(child_status),
        .child_pid = pid,
    };
}

ExecStats Executor::and_list(const AndList &and_list) const
{
    const auto lhs = this->op_list(*and_list.left);

    // Don't execute the rhs if the lhs terminated with an error
    if (lhs.exit_code != 0)
    {
        return lhs;
    }

    const auto rhs = this->op_list(*and_list.right);

    return rhs;
}

ExecStats Executor::or_list(const OrList &or_list) const
{
    const auto lhs = this->op_list(*or_list.left);

    // Don't execute the rhs if the lhs terminated with a success
    if (lhs.exit_code == 0)
    {
        return lhs;
    }

    const auto rhs = this->op_list(*or_list.right);

    return rhs;
}

ExecStats Executor::pipeline(const Pipeline &pipeline, const CommandState &state) const
{
    CommandState left_state{state};

    if (pipeline.left.has_value())
    {
        auto pipefd = create_pipe();
        if (!pipefd.has_value())
        {
            return {.exit_code = 1};
        }

        const auto [reader_fd, writer_fd] = *pipefd;

        left_state.redirects.emplace_back(STDIN_FILENO, reader_fd);
        left_state.fd_to_close.emplace_back(writer_fd);

        this->pipeline(**pipeline.left, {.inside_pipeline = true, .redirects = {{STDOUT_FILENO, writer_fd}}, .fd_to_close = {reader_fd}});
    }

    auto retval = this->command(*pipeline.right, left_state);

    if (pipeline.negated)
    {
        retval.exit_code = (retval.exit_code != 0) ? 0 : 1;
    }

    return retval;
}

ExecStats Executor::op_list(const OpList &list) const
{
    const auto stats = std::visit(
        overloads{
            [&](const AndList &and_list)
            { return this->and_list(and_list); },
            [&](const OrList &or_list)
            { return this->or_list(or_list); },
            [&](const Pipeline &pipeline)
            { return this->pipeline(pipeline, {}); },
        },
        list);

    return stats;
}

ExecStats Executor::sequential_list(const SequentialList &sequential_list) const
{
    if (sequential_list.left.has_value())
    {
        this->sequential_list(**sequential_list.left);
    }

    const auto stats = this->op_list(*sequential_list.right);
    return stats;
}

ExecStats Executor::command(const Command &command, const CommandState &state) const
{
    const auto stats = std::visit(
        overloads{
            [&](const SimpleCommand &cmd)
            { return this->simple_command(cmd, state); },
            [&](const Subshell &subshell)
            { return this->subshell(subshell, state); },
        },
        command);

    return stats;
}

ExecStats Executor::subshell(const Subshell &subshell, const CommandState &state) const
{
    // Close the redirections used by the child, the parent no longer needs
    // them. The unneeded files will be automatically closed when the
    // destructor will be called.
    RedirectController redirect{state};

    if (!redirect.add_redirects(subshell.redirections))
    {
        return {.exit_code = 1};
    }

    pid_t pid = fork();
    if (pid == -1)
    {
        throw std::runtime_error(std::string("fork failed: ") + std::strerror(errno));
    }

    if (pid == 0)
    {
        // -----------
        // Child
        // -----------

        if (!redirect.apply_redirections())
            exit(1);

        const auto child_status = this->sequential_list(*subshell.seq_list);
        exit(child_status.exit_code);
    }

    // -----------
    // Parent
    // -----------

    if (state.inside_pipeline)
    {
        // TODO: mofiy this logic
        // Don't wait for the child
        return {.child_pid = pid};
    }

    int wstatus{};
    waitpid(pid, &wstatus, 0);

    if (!WIFEXITED(wstatus))
    {
        throw std::runtime_error("Child did not return!");
    }

    return {
        .exit_code = WEXITSTATUS(wstatus),
        .child_pid = pid,
    };
}

ExecStats Executor::program(const ThisProgram &program) const
{
    if (program.child.empty())
        return {};

    ExecStats retval{};
    for (const auto &complete_command : program.child)
    {
        retval = this->sequential_list(complete_command);
    }

    return retval;
}

bool Executor::line_has_continuation() const
{
    assert(!this->input_buffer.empty());

    UnbufferedTokenizer tokenizer{this->input_buffer.back()};

    TokenType prev = TokenType::eof;
    TokenType next =
        tokenizer
            .next_token()
            .transform([](const Token &&token)
                       { return token.type; })
            .value_or(TokenType::eof);

    while (next != TokenType::eof && next != TokenType::new_line)
    {
        prev = next;

        next =
            tokenizer
                .next_token()
                .transform([](const Token &token)
                           { return token.type; })
                .value_or(TokenType::eof);
    }

    return prev == TokenType::line_continuation ||
           prev == TokenType::and_and ||
           prev == TokenType::or_or ||
           prev == TokenType::pipe;
}

bool Executor::read_stdin()
{
    std::string new_line;
    std::getline(std::cin, new_line);

    if (!std::cin)
    {
        // TODO: check if we are actually at eof
        // if (std::cin.eof())

        return false;
    }

    new_line += "\n";
    this->input_buffer.emplace_back(std::move(new_line));

    return true;
}

std::string Executor::simple_substitution(const SimpleSubstitution &prog) const
{
    // Setup piping for stdout redirections
    auto pipefd = create_pipe();
    if (!pipefd)
        throw std::runtime_error("pipe creation failed");

    const auto [reader_fd, writer_fd] = *pipefd;

    const pid_t pid = fork();
    if (pid == -1)
        throw std::runtime_error(std::format("fork failed: {}", std::strerror(errno)));

    if (pid == 0)
    {
        // -----------
        // Child
        // -----------

        close(reader_fd);
        dup2(writer_fd, STDOUT_FILENO);

        const auto stats = this->program(prog.prog);

        close(writer_fd);
        exit(stats.exit_code);
    }

    // -----------
    // Parent
    // -----------

    close(writer_fd);

    fd_streambuf buf{reader_fd};
    std::istream is(&buf);

    std::string substitution(std::istreambuf_iterator<char>(is), {});

    close(reader_fd);

    // The standard states that if the stdout of a command
    // end with a newline this has to be removed.
    if (!substitution.empty() && substitution.back() == '\n')
    {
        return substitution.substr(0, substitution.length() - 1);
    }

    return substitution;
}

// For substitution details take a look at the standard.
// https://pubs.opengroup.org/onlinepubs/9699919799/utilities/V3_chap02.html#tag_18_06_03
std::string Executor::cmd_substitution(const CmdSubstitution &cmd) const
{
    std::vector<Token> tokens{};
    std::vector<std::string> str_buffer{};

    for (const auto &inner : cmd.child)
    {
        Token token = std::visit(
            overloads{
                [&](const CmdSubstitution &cmd)
                {
                    auto tok = "'" + this->cmd_substitution(cmd) + "'";
                    auto &ref = str_buffer.emplace_back(std::move(tok));

                    return Token{
                        .type = TokenType::quoted_word,
                        .value = ref,
                    };
                },
                [&](const SimpleSubstitution &prog)
                {
                    auto tok = "'" + this->simple_substitution(prog) + "'";
                    auto &ref = str_buffer.emplace_back(std::move(tok));

                    return Token{
                        .type = TokenType::quoted_word,
                        .value = ref,
                    };
                },
                [&](const Token &token)
                { return token; },
            },
            inner);

        tokens.emplace_back(std::move(token));
    }

    TokenIter token_stream{tokens};

    // Process all the tokens
    auto upper_command = SyntaxTree<TokenIter>().program(token_stream);

    // TODO: error handling
    if (!upper_command)
        throw std::runtime_error("Failed to parse command substitution");

    std::println(stderr, "=== SUBS ===");
    std::println(stderr, "{:#?}", upper_command);

    std::string subs = this->simple_substitution({
        .start = cmd.start,
        .end = cmd.end,
        .prog = std::move(*upper_command),
    });

    return "'" + subs + "'";
}

void Executor::substitution_run(std::vector<std::string> &support) const
{
    using Sub = std::tuple<size_t, size_t, size_t, size_t, std::string>;

    // Kees the substitution in a vecotr, then apply them to the support
    // vector. The reason for this is that, if the support strings where
    // changed while the iteration takes place, the references inside the
    // tokenizer to the strings in the support vecotr, whould be invalidated
    // creating references to garbage memory.
    std::vector<Sub> subs;

    // Try and substitute the programs in the support strings
    Tokenizer process_subs_tok{support};

    while (!process_subs_tok.next_is_eof())
    {
        bool parse_substitution = false;

        // Consume the tokenizer until we reach the opening of substitution command
        while (auto subs_start = process_subs_tok.peek())
        {
            if (subs_start->type == TokenType::eof)
                break;

            if (subs_start->type == TokenType::andopen)
            {
                parse_substitution = true;
                break;
            }

            process_subs_tok.next_token();
        }

        if (!parse_substitution)
            break;

        Tokenizer closing_tok{process_subs_tok};
        const auto cmd = SyntaxTree<Tokenizer>().cmd_substitution(closing_tok);
        // TODO: error handling
        if (!cmd)
            throw std::runtime_error("could not parse command substitution");

        std::println(stderr, "=== CMD SUBSTITUION ===");
        std::println(stderr, "{:#?}", cmd);

        // TODO: this part needs to be structured better
        const Token starting_token = process_subs_tok.next_token().value();

        size_t start_vec = support.size() - process_subs_tok.buffer_size();
        size_t start_str = starting_token.start;

        // Get the previous state of the tokenizer and get
        const Token ending_token = closing_tok.prev().value().next_token().value();

        size_t end_vec = support.size() - closing_tok.buffer_size();
        size_t end_str = ending_token.end;

        // Get the stdout of the child shell
        std::string substitution = this->cmd_substitution(*cmd);

        subs.emplace_back(start_vec, start_str, end_vec, end_str, std::move(substitution));

        // Commit advancement to the tokenizer
        process_subs_tok = closing_tok;
    }

    size_t offset{};

    // Apply substitutions
    for (auto sub : subs)
    {
        auto [start_vec, start_str, end_vec, end_str, substitution] = sub;

        start_str += offset;
        end_str += offset;

        // TODO: handle multilines later
        support[start_vec] = support[start_vec].substr(0, start_str) + substitution + support[end_vec].substr(end_str);

        offset += (substitution.size()) - (end_str - start_str);
    }

    std::println(stderr, "=== AFTER SUBS ===");
    std::println(stderr, "{:#?}", support);
}

std::vector<std::string> Executor::process_input() const
{
    // Create a support buffer where the line_continuations are cut
    // and the subsequent lines are pasted togheter
    std::vector<std::string> support;

    // TODO: move in its own logic
    for (const auto &line : this->input_buffer)
    {
        if (support.empty())
        {
            support.emplace_back(line);
            continue;
        }

        std::string &last = support.back();
        std::string end = last.substr(std::max<size_t>(0, last.size() - 2));

        if (end == "\\\n")
            last = last.substr(0, last.size() - 2) + line;
        else
            support.emplace_back(line);
    }

    this->substitution_run(support);

    return support;
}

ExecStats Executor::execute() const
{
    auto support = this->process_input();

    Tokenizer tokenizer{support};
    SyntaxTree<Tokenizer> tree;

    // TODO: modify this
    if (tokenizer.next_is_eof())
        return {};

    const auto program = tree.program(tokenizer);

    if (!program.has_value())
        throw std::runtime_error("Parsing failed!");

    std::println(stderr, "=== SYNTAX TREE ===");
    std::println(stderr, "{:#?}", *program);

    std::println(stderr, "=== COMMAND BEGIN ===");

    const auto retval = this->program(*program);

    return retval;
}

TerminalState Executor::update()
{
    if (!this->read_stdin())
    {
        return TerminalState{
            .terminate_session = true,
        };
    }

    if (this->line_has_continuation())
    {
        return {.needs_more = true};
    }

    const auto exec_stats = this->execute();
    this->input_buffer.clear();

    return {
        .exit_code = exec_stats.exit_code,
    };
}