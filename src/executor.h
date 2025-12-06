#ifndef TESTSH_EXECUTOR_H
#define TESTSH_EXECUTOR_H

#include "tokenizer.h"
#include "syntax.h"
#include <string_view>
#include <vector>
#include <tuple>
#include "shell.h"
#include "job.h"

struct TerminalState
{
    bool terminate_session;
    bool needs_more;
    int exit_code;
};

struct CommandState
{
    std::vector<std::tuple<int, int>> redirects;
    std::vector<int> fd_to_close;
    bool exec_async = false;
    // TODO: modify to optional?
    int pipeline_pgid = -1;

    bool initialized() const
    { return !redirects.empty(); }
};

class Executor
{
    std::vector<std::string> input_buffer;
    Shell shell{};
    // TerminalState terminal_state;

    std::optional<ExecStats> builtin(const SimpleCommand &cmd) const;
    ExecStats simple_command(const SimpleCommand &cmd, const CommandState &state) const;
    ExecStats and_list(const AndList &and_list, const CommandState &state) const;
    ExecStats or_list(const OrList &or_list, const CommandState &state) const;
    Job pipeline(const Pipeline &pipeline, const CommandState &state) const;
    ExecStats foreground_pipeline(const Pipeline &pipeline, const CommandState &state) const;
    ExecStats op_list(const OpList &list, const CommandState &state) const;
    ExecStats sequential_list(const SequentialList &sequential_list, const CommandState &state) const;
    ExecStats async_list(const AsyncList &async_list, const CommandState &state) const;
    ExecStats list(const List &list, const CommandState &state) const;
    ExecStats command(const Command &command, const CommandState &state) const;
    ExecStats subshell(const Subshell &subshell, const CommandState &state) const;
    ExecStats program(const ThisProgram &program) const;

    bool line_has_continuation() const;
    bool read_stdin();
    std::string simple_substitution(const SimpleSubstitution &prog) const;
    std::string cmd_substitution(const CmdSubstitution &cmd) const;
    void substitution_run(std::vector<std::string> &support) const;
    std::vector<std::string> process_input() const;
    ExecStats execute() const;

public:
    TerminalState update();
};

#endif // TESTSH_EXECUTOR_H