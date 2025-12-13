#ifndef TESTSH_EXEC_PROG_H
#define TESTSH_EXEC_PROG_H

#include "shell.h"
#include "syntax.h"
#include <memory>
#include <string>
#include <vector>

class Exec {
    typedef const char *char_array_t[];

    std::vector<std::string> args_owner;
    std::unique_ptr<char_array_t> args_array;
    std::size_t args_size;

    std::vector<std::string> envp_owner;
    std::unique_ptr<char_array_t> envp_array;
    std::size_t envp_size;

    void init_args(const SimpleCommand &cmd);

    void init_envp(const SimpleCommand &cmd, const Shell &shell);

  public:
    explicit Exec(const SimpleCommand &cmd, const Shell &shell);

    int exec() const;
};

#endif // TESTSH_EXEC_PROG_H
