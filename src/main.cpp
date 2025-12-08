#include "executor.h"
#include <iostream>
#include <ostream>
#include <print>
#include <sys/wait.h>
#include <unistd.h>

constexpr std::string_view red = "\033[31m";
constexpr std::string_view reset = "\033[0m";

int main() {
    TerminalState state{};
    Executor executor{};

    while (true) {
        if (state.terminate_session) {
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
