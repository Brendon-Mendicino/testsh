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
#include "parser/executor.h"

namespace rng = std::ranges::views;

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

        if (state.needs_more)
            std::print("> ");
        else
            std::print("$ ");
        std::cout << std::flush;

        state = executor.update();
    }

    return 0;
}
