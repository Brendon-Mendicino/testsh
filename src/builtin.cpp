#include "builtin.h"
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <print>
#include <unistd.h>

namespace fs = std::filesystem;

int builtin_cd(const SimpleCommand &cd) {
    fs::path target{};

    if (cd.arguments.empty() || cd.arguments[0].value == "~") {
        const char *home = std::getenv("HOME");

        if (home == nullptr) {
            std::println(stderr, "cd: $HOME not set");
            return 1;
        }

        target = home;
    } else if (cd.arguments.size() == 1) {
        target = cd.arguments[0].text();
    } else {
        std::println(stderr, "cd: too many arguments");
        return 1;
    }

    // Change current path in the application
    std::error_code ec;
    fs::current_path(target, ec);

    if (ec) {
        std::println(stderr, "cd: {}: {}", target.string(), ec.message());
        return 1;
    }

    return 0;
}

int builtin_exec(const SimpleCommand &exec) {
    assert(exec.program.text() == "exec");

    if (exec.arguments.size() < 1) {
        return 0;
    }

    // Crate a support class, with the first arg moved to be the program name.
    // This makes it easier to get args to pass the the execvp()
    const SimpleCommand to_exec{
        .program = exec.arguments[0],
        .arguments =
            std::vector(exec.arguments.begin() + 1, exec.arguments.end()),
    };

    ArgsToExec exec_feed{to_exec};
    const auto &args = exec_feed.args()->get();

    const int retval = execvp(args[0], (char *const *)args);

    std::println(stderr, "exec: {}: {}", args[0], std::strerror(errno));
    return retval;
}

int builtin_exit(const SimpleCommand &exit) {
    assert(exit.program.text() == "exit");

    if (exit.arguments.size() > 1) {
        std::println(stderr, "exit: too many arguments");
        return 1;
    }

    int exit_code{};

    if (exit.arguments.size() == 1) {
        std::string tmp{exit.arguments[0].text()};
        exit_code = std::atoi(tmp.c_str());
    } else {
        exit_code = 1;
    }

    std::exit(exit_code);
}
