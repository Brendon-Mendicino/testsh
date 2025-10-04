#include <complex>
#include <string>
#include <iostream>
#include <print>
#include <ostream>
#include <vector>
#include <unistd.h>
#include <sys/wait.h>
#include <memory>
#include <ranges>
#include <cstring>
#include "re2/re2.h"
#include "parser/syntax.h"

namespace rng = std::ranges::views;

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

struct Terminal
{
    int status;
};

int main()
{
    std::string line{};
    Terminal term_state{};

    while (true)
    {
        if (term_state.status != 0)
        {
            std::println("PREV STATUS: {}", term_state.status);
        }

        std::print("$ ");
        std::cout << std::flush;

        std::getline(std::cin, line);

        SyntaxTree tree{line};

        const auto prog{tree.build()};

        std::println("dbg: {:?}", prog);

        // if (std::cout.eof() || args[0] == "exit")
        //     break;

        const pid_t pid = vfork();

        if (pid == -1)
        {
            exit(1);
        }

        if (pid == 0)
        {
            const ArgsToExec exec{prog};
            const auto args_to_pass = exec.args()->get();

            execvp(args_to_pass[0], (char *const *)args_to_pass);
        }

        int child_status;
        pid_t child_wait = waitpid(pid, &child_status, 0);
        if (child_wait != pid)
            throw std::runtime_error(std::format("The wait returened an error! retval={}", child_wait));

        term_state.status = WEXITSTATUS(child_status);
    }

    return 0;
}
