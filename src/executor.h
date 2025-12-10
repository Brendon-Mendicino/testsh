#ifndef TESTSH_EXECUTOR_H
#define TESTSH_EXECUTOR_H

#include "job.h"
#include "shell.h"
#include "syntax.h"
#include "util.h"
#include <format>
#include <string_view>
#include <tuple>
#include <vector>

enum class SpawnType {
    command,
    subshell,
    async_list,

};

struct TerminalState {
    bool terminate_session;
    bool needs_more;
    int exit_code;
};

struct CommandState {
    std::vector<std::tuple<int, int>> redirects{};
    std::vector<int> fd_to_close{};
    bool is_foreground = true;
    bool inside_pipeline = false;
    // TODO: modify to optional?
    int pipeline_pgid = -1;

    bool initialized() const { return !redirects.empty(); }
};

struct ListStats {
    ExecStats last_stats;
    std::vector<Job> bg_jobs;
};

struct Executor {
    std::vector<std::string> input_buffer;
    Shell shell{};
    std::vector<Job> bg_jobs;
    // TerminalState terminal_state;

    std::optional<ExecStats> builtin(const SimpleCommand &cmd) const;
    ExecStats simple_command(const SimpleCommand &cmd,
                             const CommandState &state) const;
    ExecStats and_list(const AndList &and_list,
                       const CommandState &state) const;
    ExecStats or_list(const OrList &or_list, const CommandState &state) const;
    Job pipeline(const Pipeline &pipeline, const CommandState &state) const;
    ExecStats wait_pipeline(const Pipeline &pipeline,
                            const CommandState &state) const;
    ExecStats op_list(const OpList &list, const CommandState &state) const;
    ListStats sequential_list(const SequentialList &sequential_list,
                              const CommandState &state) const;
    ListStats async_list(const AsyncList &async_list,
                         const CommandState &state) const;
    ListStats list(const List &list, const CommandState &state) const;
    ExecStats command(const Command &command, const CommandState &state) const;
    ExecStats subshell(const Subshell &subshell,
                       const CommandState &state) const;
    ExecStats program(const ThisProgram &program);

    bool line_has_continuation() const;
    bool read_stdin();
    std::string simple_substitution(const SimpleSubstitution &prog);
    std::string cmd_substitution(const CmdSubstitution &cmd);
    void substitution_run(std::vector<std::string> &support);
    std::vector<std::string> process_input();
    ExecStats execute();

    TerminalState update();
    void loop();
};

// ------------------------------------
// FORMATTER
// ------------------------------------

constexpr std::string_view to_string(const SpawnType &t) {
    switch (t) {
    case SpawnType::command:
        return "command";
    case SpawnType::subshell:
        return "subshell";
    case SpawnType::async_list:
        return "async_list";
    }
}

template <typename CharT> struct std::formatter<SpawnType, CharT> : debug_spec {
    auto format(const SpawnType &s, auto &ctx) const {
        return std::format_to(ctx.out(), "{}", to_string(s));
    }
};

template <typename CharT>
struct std::formatter<CommandState, CharT> : debug_spec {
    auto format(const CommandState &c, auto &ctx) const {
        this->start<CommandState>(ctx);
        this->field("redirects", c.redirects, ctx);
        this->field("fd_to_close", c.fd_to_close, ctx);
        this->field("is_foreground", c.is_foreground, ctx);
        this->field("inside_pipeline", c.inside_pipeline, ctx);
        this->field("pipeline_pgid", c.pipeline_pgid, ctx);
        return this->finish(ctx);
    }
};

#endif // TESTSH_EXECUTOR_H
