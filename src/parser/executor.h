#ifndef TESTSH_EXECUTOR_H
#define TESTSH_EXECUTOR_H

#include "tokenizer.h"
#include "syntax.h"
#include <string_view>
#include <vector>

struct TerminalState
{
    bool terminate_session;
    bool needs_more;
    int exit_code;
};

struct ExecStats
{
    int exit_code;
};

class Executor
{
    std::vector<std::string> input_buffer;
    TerminalState terminal_state;

    std::optional<ExecStats> builtin(const Program &prog) const;
    ExecStats execute_program(const Program &prog) const;
    ExecStats negate(const Program &prog) const;
    ExecStats and_list(const AndList &and_list) const;
    ExecStats or_list(const OrList &or_list) const;
    ExecStats words(const Words &words) const;
    ExecStats pipeline(const Pipeline &pipeline) const;
    ExecStats op_list(const OpList &list) const;
    ExecStats sequential_list(const SequentialList &sequential_list) const;
    ExecStats command(const Command &command) const;
    ExecStats subshell(const Subshell &subshell) const;

    bool line_has_continuation() const;
    bool read_stdin();
    ExecStats execute() const;

public:
    TerminalState update();
};

#endif // TESTSH_EXECUTOR_H