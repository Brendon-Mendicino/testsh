#include "builtin.h"
#include <filesystem>
#include <print>
#include <cstdio>
#include <unistd.h>
#include <cstdio>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <cstdlib>

namespace fs = std::filesystem;

// TODO: move this class and the other class
class ArgsToExec
{
    typedef const char *args_array_t[];

    std::vector<std::string> str_owner;
    std::unique_ptr<args_array_t> args_array;
    std::size_t args_size;

public:
    explicit ArgsToExec(const SimpleCommand &cmd)
    {
        const auto &args = cmd.arguments;

        // +1 from program
        // +1 from NULL terminator
        this->args_size = args.size() + 2;

        this->args_array = std::make_unique<args_array_t>(this->args_size);

        this->str_owner.reserve(args.size() + 2);

        // Push program first
        this->str_owner.emplace_back(cmd.program);
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

int builtin_cd(const std::vector<std::string_view> &args)
{
    fs::path target{};

    if (args.empty() || args[0] == "~")
    {
        const char *home = std::getenv("HOME");

        if (home == nullptr)
        {
            std::println(stderr, "cd: $HOME not set");
            return 1;
        }

        target = home;
    }
    else if (args.size() == 1)
    {
        target = args[0];
    }
    else
    {
        std::println(stderr, "cd: too many arguments");
        return 1;
    }

    // Change current path in the application
    std::error_code ec;
    fs::current_path(target, ec);

    if (ec)
    {
        std::println(stderr, "cd: {}: {}", target.string(), ec.message());
        return 1;
    }

    return 0;
}

int builtin_exec(const SimpleCommand &exec)
{
    assert(exec.program == "exec");

    if (exec.arguments.size() < 1)
    {
        return 0;
    }

    // Crate a support class, with the first arg moved to be the program name.
    // This makes it easier to get args to pass the the execvp()
    const SimpleCommand to_exec{
        .program = exec.arguments[0],
        .arguments = std::vector(exec.arguments.begin() + 1, exec.arguments.end()),
    };

    ArgsToExec exec_feed{to_exec};
    const auto &args = exec_feed.args()->get();

    const int retval = execvp(args[0], (char *const *)args);

    std::println(stderr, "exec: {}: {}", args[0], std::strerror(errno));
    return retval;
}

int builtin_exit(const SimpleCommand &exit)
{
    assert(exit.program == "exit");

    if (exit.arguments.size() > 1)
    {
        std::println(stderr, "exit: too many arguments");
        return 1;
    }

    int exit_code{};

    if (exit.arguments.size() == 1)
    {
        std::string tmp{exit.arguments[0]};
        exit_code = std::atoi(tmp.c_str());
    }
    else
    {
        exit_code = 1;
    }

    std::exit(exit_code);
}
