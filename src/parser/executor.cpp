#include "executor.h"
#include "../util.h"
#include "../builtin/builtin.h"
#include <iostream>
#include <string>
#include <variant>
#include <memory>
#include <unistd.h>
#include <wait.h>
#include <print>
#include <cerrno>
#include <stdexcept>
#include <cstring>

class ArgsToExec
{
    typedef const char *args_array_t[];

    std::vector<std::string> str_owner;
    std::unique_ptr<args_array_t> args_array;
    std::size_t args_size;

public:
    explicit ArgsToExec(const Program &prog)
    {
        const auto &args = prog.arguments;

        // +1 from program
        // +1 from NULL terminator
        this->args_size = args.size() + 2;

        this->args_array = std::make_unique<args_array_t>(this->args_size);

        this->str_owner.reserve(args.size() + 2);

        // Push program first
        this->str_owner.emplace_back(prog.program);
        this->args_array[0] = this->str_owner[0].c_str();

        for (size_t i{}; i < args.size(); ++i)
        {
            auto &s = this->str_owner.emplace_back(args[i]);
            this->args_array[i + 1] = s.c_str();
        }

        // The args array needs to be null-terminated
        this->args_array[this->args_size - 1] = nullptr;
    }

    [[nodiscard]] auto args() const &
    {
        return &this->args_array;
    }
};

// TODO: Move this class with RAII
static bool apply_redirections(const CommandState &state)
{
    // close fds: leftover from the pipe() calls
    for (const auto to_close : state.fd_to_close)
    {
        close(to_close);
    }

    // Duplicate fds
    for (const auto [to_replace, replacer] : state.redirects)
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

static void close_redirections(const CommandState &state)
{
    for (const auto [_, replacer] : state.redirects)
    {
        close(replacer);
    }
}

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

std::optional<ExecStats> Executor::builtin(const Program &prog) const
{
    if (prog.program == "cd")
    {
        const int exit_code = builtin_cd(prog.arguments);
        return ExecStats{
            .exit_code = exit_code,
        };
    }

    else if (prog.program == "exec")
    {
        const int exit_code = builtin_exec(prog);
        return ExecStats{
            .exit_code = exit_code,
        };
    }

    else if (prog.program == "exit")
    {
        const int exit_code = builtin_exit(prog);
        return ExecStats{
            .exit_code = exit_code,
        };
    }

    return std::nullopt;
}

ExecStats Executor::execute_program(const Program &prog, const CommandState &state) const
{
    // Check if a builtin can be run first before, before running
    // the program through exec().
    const auto builtin_run = this->builtin(prog);
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

        if (!apply_redirections(state))
            exit(1);

        const ArgsToExec exec{prog};
        const auto args_to_pass = exec.args()->get();

        execvp(args_to_pass[0], (char *const *)args_to_pass);
        exit(1);
    }


    // -----------
    // Parent
    // -----------

    // Close the redirections used by the child, the parent no longer needs
    // them
    close_redirections(state);

    if (state.inside_pipeline)
    {
        // TODO: mofiy this logic
        // Don't wait for the child
        return { .child_pid = pid };
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

// TODO: move to builtin
ExecStats Executor::negate(const Program &prog, const CommandState &state) const
{
    auto stats = this->execute_program(prog, state);

    stats.exit_code = (stats.exit_code == 0) ? 1 : 0;

    return stats;
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

ExecStats Executor::words(const Words &words, const CommandState &state) const
{
    const auto stats = std::visit(
        overloads{
            [&](const Program &program)
            { return this->execute_program(program, state); },
            [&](const StatusNeg &status_neg)
            { return this->negate(status_neg.prog, state); },
        },
        words);

    return stats;
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

    const auto retval = this->command(*pipeline.right, left_state);

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
            [&](const Words &words)
            { return this->words(words, state); },
            [&](const Subshell &subshell)
            { return this->subshell(subshell, state); },
        },
        command);

    return stats;
}

ExecStats Executor::subshell(const Subshell &subshell, const CommandState &state) const
{
    // TODO: move forking in it's own function
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

        if (!apply_redirections(state))
            exit(1);

        const auto child_status = this->sequential_list(*subshell.seq_list);
        exit(child_status.exit_code);
    }

    // -----------
    // Parent
    // -----------

    // Close the redirections used by the child, the parent no longer needs
    // them
    close_redirections(state);

    if (state.inside_pipeline)
    {
        // TODO: mofiy this logic
        // Don't wait for the child
        return { .child_pid = pid };
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

    while (next != TokenType::eof)
    {
        prev = next;

        next =
            tokenizer
                .next_token()
                .transform([](const Token &&token)
                           { return token.type; })
                .value_or(TokenType::eof);
    }

    return prev == TokenType::line_continuation || prev == TokenType::and_and || prev == TokenType::or_or;
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

    this->input_buffer.emplace_back(std::move(new_line));

    return true;
}

ExecStats Executor::execute() const
{
    // Create a support buffer where the line_continuations are cut
    // and the subsequent lines are pasted togheter
    std::vector<std::string> support;

    for (const auto &line : this->input_buffer)
    {
        if (support.size() == 0)
        {
            support.emplace_back(line);
            continue;
        }

        auto &last = support.back();

        if (last.back() == '\\')
            last = last.substr(0, last.size() - 1) + line;
        else
            support.emplace_back(line);
    }

    Tokenizer tokenizer{support};
    SyntaxTree tree;

    // TODO: modify this
    if (tokenizer.next_is_eof())
        return {};

    const auto program = tree.build(tokenizer);

    if (!program.has_value())
        throw std::runtime_error("Parsing failed!");

    std::println("=== SYNTAX TREE ===");
    std::println("{:#?}", *program);

    std::println("=== COMMAND BEGIN ===");

    const auto retval = this->sequential_list(*program);

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