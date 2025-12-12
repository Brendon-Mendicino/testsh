#include "exec_prog.h"
#include <cstddef>
#include <print>
#include <unistd.h>

void Exec::init_args(const SimpleCommand &cmd) {
    const auto &args = cmd.arguments;

    // +1 from program
    // +1 from NULL terminator
    this->args_size = args.size() + 2;

    this->args_array = std::make_unique<char_array_t>(this->args_size);

    this->args_owner.reserve(args.size() + 2);

    // Push program first
    this->args_owner.emplace_back(cmd.program.text());
    this->args_array[0] = this->args_owner[0].c_str();

    for (size_t i{}; i < args.size(); ++i) {
        auto &s = this->args_owner.emplace_back(args[i].text());
        this->args_array[i + 1] = s.c_str();
    }

    // The args array needs to be null-terminated
    this->args_array[this->args_size - 1] = nullptr;
}

void Exec::init_envp(const SimpleCommand &cmd) {
    const auto &envp = cmd.envs;
    size_t curr_env_size{};

    /* If envp_size is 0 use the environ (unistd) variable directly to inject
     * into exec.
     */
    if (envp.size() == 0) {
        this->envp_size = 0;
        return;
    }

    // Count the current environs
    for (size_t i = 0; environ[i] != nullptr; i++)
        this->envp_size += 1;

    curr_env_size = this->envp_size;

    this->envp_size += envp.size();
    // +1 for NULL terminator
    this->envp_size += 1;

    std::println(stderr, "envp_size={}", envp_size);

    this->envp_array = std::make_unique<char_array_t>(this->envp_size);

    // Copy current environment
    for (size_t i = 0; i < curr_env_size; i++)
        this->envp_array[i] = environ[i];

    this->envp_owner.reserve(envp.size());

    // Append extra entries
    for (size_t i = 0; i < envp.size(); i++) {
        auto &s = this->envp_owner.emplace_back(envp[i].whole.text());
        this->envp_array[i + curr_env_size] = s.c_str();
    }

    this->envp_array[this->envp_size - 1] = nullptr;
}

const char *const *Exec::envp() const {
    return (this->envp_size == 0) ? environ : this->envp_array.get();
}

Exec::Exec(const SimpleCommand &cmd)
    : args_owner(), args_array(), args_size(), envp_owner(), envp_array(),
      envp_size() {
    init_args(cmd);
    init_envp(cmd);
}

int Exec::exec() const {
    char *const *envp = (char *const *)this->envp();
    char *const *argv = (char *const *)this->args_array.get();

    assert(argv != nullptr);
    assert(envp != nullptr);

    return execvpe(argv[0], argv, envp);
}
