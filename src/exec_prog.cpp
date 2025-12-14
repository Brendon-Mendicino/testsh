#include "exec_prog.h"
#include <cstddef>
#include <memory>
#include <ranges>
#include <unistd.h>
#include <unordered_set>

namespace vw = std::ranges::views;

// TODO: take ownership of cmd (?)
void Exec::init_args(const SimpleCommand &cmd) {
    const auto &args = cmd.arguments;

    // +1 from program
    // +1 from NULL terminator
    this->args_size = args.size() + 2;

    this->args_array = std::make_unique<char_array_t>(this->args_size);

    this->args_owner.reserve(args.size() + 2);

    // Push program first
    this->args_owner.emplace_back(cmd.program);
    this->args_array[0] = this->args_owner[0].c_str();

    for (size_t i{}; i < args.size(); ++i) {
        auto &s = this->args_owner.emplace_back(args[i]);
        this->args_array[i + 1] = s.c_str();
    }

    // The args array needs to be null-terminated
    this->args_array[this->args_size - 1] = nullptr;
}

void Exec::init_envp(const SimpleCommand &cmd, const Shell &shell) {
    std::unordered_set<std::string_view> cmd_env_names{};
    for (const auto &env : cmd.envs) {
        cmd_env_names.insert(env.key);
    }

    /* Add external envs from the shell that are not present in the current
     * command.
     */
    for (const auto &env : shell.vars) {
        if (!env.attr.external)
            continue;

        if (cmd_env_names.contains(env.name()))
            continue;

        this->envp_owner.emplace_back(env.str);
    }

    /* Now add envs from the current command. If names are duplicated, the last
     * value has to be passed down to the child. Add them in reverse order and
     * check if they are still in the set, remove it from the set after you the
     * env once, subsequent vars will be ignored.
     */
    for (const auto &cmd_env : cmd.envs | vw::reverse) {
        if (!cmd_env_names.contains(cmd_env.key))
            continue;

        cmd_env_names.erase(cmd_env.key);
        this->envp_owner.emplace_back(cmd_env.whole.text());
    }

    // +1 for NULL terminator
    this->envp_size = this->envp_owner.size() + 1;

    this->envp_array = std::make_unique<char_array_t>(this->envp_size);

    for (size_t i = 0; i < this->envp_owner.size(); i++) {
        this->envp_array[i] = this->envp_owner[i].c_str();
    }

    this->envp_array[this->envp_size - 1] = nullptr;
}

Exec::Exec(const SimpleCommand &cmd, const Shell &shell)
    : args_owner(), args_array(), args_size(), envp_owner(), envp_array(),
      envp_size() {
    init_args(cmd);
    init_envp(cmd, shell);
}

int Exec::exec() const {
    char *const *envp = (char *const *)this->envp_array.get();
    char *const *argv = (char *const *)this->args_array.get();

    assert(argv != nullptr);
    assert(argv[0] != nullptr);
    assert(envp != nullptr);

    return execvpe(argv[0], argv, envp);
}
