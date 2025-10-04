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

        Executor executor{line};

        const auto stats = executor.execute();

        term_state.status = stats.exit_code;
    }

    return 0;
}
