#include "executor.h"
#include "../util.h"
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

ExecStats Executor::execute_program(const Program &prog)
{
    const pid_t pid = fork();

    if (pid == -1)
    {
        exit(1);
    }

    if (pid == 0)
    {
        const ArgsToExec exec{prog};
        const auto args_to_pass = exec.args()->get();

        execvp(args_to_pass[0], (char *const *)args_to_pass);
        exit(1);
    }

    int child_status;
    pid_t child_wait = waitpid(pid, &child_status, 0);
    if (child_wait != pid)
        throw std::runtime_error(std::format("The wait returened an error! retval={}", child_wait));

    return ExecStats{
        .exit_code = WEXITSTATUS(child_status),
    };
}

// TODO: move to builtin
ExecStats Executor::negate(const Program &prog)
{
    auto stats = this->execute_program(prog);

    stats.exit_code = (stats.exit_code == 0) ? 1 : 0;

    return stats;
}

ExecStats Executor::and_list(const AndList &and_list)
{
    const auto lhs = this->op_list(*and_list.left);

    // Don't execute the rhs if the lhs terminated with an error
    if (lhs.exit_code != 0) {
        return lhs;
    }

    const auto rhs = this->op_list(*and_list.right);

    return rhs;
}

ExecStats Executor::or_list(const OrList &or_list)
{
    const auto lhs = this->op_list(*or_list.left);

    // Don't execute the rhs if the lhs terminated with a success
    if (lhs.exit_code == 0) {
        return lhs;
    }

    const auto rhs = this->op_list(*or_list.right);

    return rhs;
}

ExecStats Executor::words(const Words &words)
{
    const auto stats = std::visit(
        overloads {
            [&](const Program &program)
            { return this->execute_program(program); },
            [&](const StatusNeg &status_neg)
            { return this->negate(status_neg.prog); },
        },
        words);

    return stats;
}

ExecStats Executor::op_list(const OpList &list)
{
    const auto stats = std::visit(
        overloads {
            [&](const AndList &and_list)
            { return this->and_list(and_list); },
            [&](const OrList &or_list)
            { return this->or_list(or_list); },
            [&](const Command &command)
            { return this->command(command); },
        },
        list);

    return stats;
}

ExecStats Executor::sequential_list(const SequentialList &sequential_list)
{
    if (sequential_list.left.has_value())
    {
        this->sequential_list(**sequential_list.left);
    }

    const auto stats = this->op_list(*sequential_list.right);
    return stats;
}

ExecStats Executor::command(const Command &command)
{
    const auto stats = std::visit(
        overloads {
            [&](const Words &words)
            { return this->words(words); },
            [&](const Subshell &subshell)
            { return this->subshell(subshell); },
        },
        command);

    return stats;
}

ExecStats Executor::subshell(const Subshell &subshell)
{
    ExecStats retval{};

    // TODO: move forking in it's own function
    pid_t pid = fork();
    if (pid == -1)
    {
        throw std::runtime_error(std::string("fork failed: ") + std::strerror(errno));
    }

    if (pid == 0)
    {
        // Child
        const auto child_status = this->sequential_list(*subshell.seq_list);
        exit(child_status.exit_code);
    }
    else
    {
        // Parent
        int wstatus{};
        waitpid(pid, &wstatus, 0);

        if (!WIFEXITED(wstatus))
        {
            throw std::runtime_error("Child did not return!");
        }

        retval.exit_code = WEXITSTATUS(wstatus);
    }

    return retval;
}

Executor::Executor(std::string_view input) : input(input) {}

// TODO: change retval
ExecStats Executor::execute()
{
    Tokenizer tokenizer{this->input};
    SyntaxTree tree;

    const auto program = tree.build(tokenizer);

    if (!program.has_value())
        throw std::runtime_error("Parsing failed!");

    std::println("=== SYNTAX TREE ===");
    std::println("{:#?}", *program);

    std::println("=== COMMAND BEGIN ===");

    const auto retval = this->sequential_list(*program);

    return retval;
}