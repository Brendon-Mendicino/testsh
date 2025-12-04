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
#include "syntax.h"
#include "executor.h"

namespace rng = std::ranges::views;

constexpr std::string_view red = "\033[31m";
constexpr std::string_view reset = "\033[0m";

int main()
{
    TerminalState state{};
    Executor executor{};

    while (true)
    {
        if (state.terminate_session)
        {
            break;
        }

        if (state.exit_code != 0)
            std::print(red);

        if (state.needs_more)
            std::print("> ");
        else
            std::print("$ ");

        std::print(reset);
        std::cout << std::flush;

        state = executor.update();
    }

    return 0;
}
