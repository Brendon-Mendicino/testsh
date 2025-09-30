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

namespace rng = std::ranges::views;

std::vector<std::string> split(const std::string &s, const std::string &delimiter) {
    std::vector<std::string> tokens{};
    std::size_t pos{};

    while (true) {
        const auto delimiter_index = s.find(delimiter, pos);

        std::string token = s.substr(pos, delimiter_index - pos);

        if (!token.empty()) {
            tokens.push_back(token);
        }

        if (delimiter_index == std::string::npos)
            break;

        pos = delimiter_index + delimiter.length();
    }

    return tokens;
}

class ArgsToExec {
    typedef char *args_array[];

    std::unique_ptr<args_array> args_to_exec;
    std::size_t args_size;

public:
    explicit ArgsToExec(const std::vector<std::string> &args) {
        auto args_array = std::make_unique<char *[]>(args.size() + 1);

        for (int i{}; i < args.size(); ++i) {
            args_array[i] = strdup(args[i].c_str());
        }

        // The args array needs to be null-terminated
        args_array[args.size()] = nullptr;

        this->args_size = args.size() + 1;
        this->args_to_exec = std::move(args_array);
    }

    [[nodiscard]] auto args() const & {
        return &this->args_to_exec;
    }

    ~ArgsToExec() {
        for (int i{}; i < this->args_size - 1; ++i) {
            delete this->args_to_exec[i];
        }
    }
};

int main() {
    std::string line{};

    while (true) {
        std::print("$ ");
        std::cout << std::flush;

        std::getline(std::cin, line);

        const auto args = split(line, " ");

        if (args.empty()) {
            continue;
        }

        if (std::cout.eof() || args[0] == "exit") {
            break;
        }

        const pid_t pid = vfork();

        if (pid == -1) {
            exit(1);
        }

        if (pid == 0) {
            const ArgsToExec exec{args};
            const auto args_to_pass = exec.args()->get();

            execvp(args[0].c_str(), args_to_pass);
        }

        waitpid(pid, nullptr, 0);
    }

    return 0;
}
