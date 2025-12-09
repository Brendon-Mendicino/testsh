#include "executor.h"
#include <iostream>
#include <ostream>
#include <print>
#include <sys/wait.h>
#include <unistd.h>

constexpr std::string_view red = "\033[31m";
constexpr std::string_view reset = "\033[0m";

static void loop(void) {
    TerminalState state{};
    Executor executor{};

    std::println(stderr, "testsh pid: {}, shell: {:#?}", getpid(),
                 executor.shell);

    while (true) {
        if (state.terminate_session) {
            break;
        }

        if (executor.shell.is_interactive) {
            if (state.exit_code != 0)
                std::print(red);

            if (state.needs_more)
                std::print("> ");
            else
                std::print("$ ");

            std::print(reset);
            std::cout << std::flush;
        }

        state = executor.update();
    }
}

int main() {
    loop();

    return 0;
}
