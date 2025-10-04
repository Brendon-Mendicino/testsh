#include "executor.h"
#include "../util.h"
#include <variant>
#include <memory>
#include <unistd.h>
#include <wait.h>
#include <print>

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

Executor::Executor(std::string_view input) : input(input) {}

// TODO: change retval
ExecStats Executor::execute()
{
    Tokenizer tokenizer{this->input};
    SyntaxTree tree;

    const auto script = tree.build(tokenizer);

    if (!script.has_value())
        throw std::runtime_error("Parsing failed!");

    std::println("{:#?}", *script);

    const auto stats = std::visit(
        overloads{
            [&](const Program &p)
            { return this->execute_program(p); },
            [&](const StatusNeg &p)
            { return this->negate(p.prog); },
        },
        script->value);

    return stats;
}