#include "executor.h"
#include "builtin.h"
#include "exec_prog.h"
#include "job.h"
#include "syntax.h"
#include "util.h"
#include <algorithm>
#include <cerrno>
#include <cpptrace/cpptrace.hpp>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <functional>
#include <iostream>
#include <memory>
#include <print>
#include <ranges>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>
#include <variant>
#include <wait.h>

namespace vw = std::ranges::views;

static bool fd_is_valid(int fd) {
    return fcntl(fd, F_GETFD) != -1 || errno != EBADF;
}

/**
 * This class destructor automatically calls close() on all the
 * open file descriptors that the parent doesn't need to handle.
 *
 */
class RedirectController {
    // tuple<to_replace, replacer>
    std::vector<std::tuple<int, int>> file_redirects;
    // tuple<to_replace, replacer>
    std::vector<std::tuple<int, int>> duplications;
    std::vector<int> fd_to_close;

  public:
    RedirectController(const CommandState &state)
        : file_redirects(state.redirects), duplications(),
          fd_to_close(state.fd_to_close) {}

    RedirectController(const RedirectController &f) = delete;
    RedirectController(RedirectController &&f) = delete;
    RedirectController &operator=(const RedirectController &f) = delete;
    RedirectController &operator=(RedirectController &&f) = delete;

    ~RedirectController() {
        // When the object is destructed only close the fds that are originated
        // from a file. The duplications<> must not be touched by the parent,
        // redirections are only need for the child. For this reason
        // the duplicatoion fds on the parent must remain intact.
        for (const auto [_, replacer] : this->file_redirects) {
            close(replacer);
        }
    }

    bool add_redirects(const std::vector<Redirect> &redirections) {
        for (const auto &redirect : redirections) {
            bool success = std::visit(
                overloads{
                    [&](const FileRedirect &file_r) {
                        int flags{};

                        switch (file_r.file_kind) {
                        case OpenKind::read:
                            flags = O_RDONLY;
                            break;

                        case OpenKind::replace:
                            flags = O_CREAT | O_TRUNC | O_WRONLY;
                            break;

                        case OpenKind::append:
                            flags = O_CREAT | O_APPEND | O_WRONLY;
                            break;

                        case OpenKind::rw:
                            flags = O_CREAT | O_RDWR;
                            break;
                        }

                        std::string tmp_filename{file_r.filename};
                        mode_t mode =
                            S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH;
                        int open_fd = open(tmp_filename.c_str(), flags, mode);

                        if (open_fd == -1) {
                            std::println(stderr, "open: {}",
                                         std::strerror(errno));
                            return false;
                        }

                        this->file_redirects.emplace_back(file_r.redirect_fd,
                                                          open_fd);
                        return true;
                    },
                    [&](const FdRedirect &dup_fd) {
                        // Check if the replacer fd exists
                        if (!fd_is_valid(dup_fd.fd_replacer)) {
                            std::println(
                                stderr,
                                "testsh: file descriptor {} does not exist",
                                dup_fd.fd_replacer);
                            return false;
                        }

                        this->duplications.emplace_back(dup_fd.fd_to_replace,
                                                        dup_fd.fd_replacer);
                        return true;
                    },
                    [&](const CloseFd &close_fd) {
                        this->fd_to_close.emplace_back(close_fd.fd);
                        return true;
                    },
                },
                redirect);

            if (!success)
                return false;
        }

        return true;
    }

    bool apply_redirections() const {
        // close fds: leftover from the pipe() calls
        for (const auto to_close : this->fd_to_close) {
            close(to_close);
        }

        // Duplicate fds from files
        for (const auto [to_replace, replacer] : this->file_redirects) {
            const int retval = dup2(replacer, to_replace);
            if (retval == -1) {
                std::println(stderr, "dup2: {}", std::strerror(errno));
                return false;
            }

            // close(replacer);
        }

        // Duplicate fds from duplication syntax
        for (const auto [to_replace, replacer] : this->duplications) {
            const int retval = dup2(replacer, to_replace);
            if (retval == -1) {
                std::println(stderr, "dup2: {}", std::strerror(errno));
                return false;
            }
        }

        return true;
    }
};

// ------------------------------------
// Waiter
// ------------------------------------

void Waiter::process_wstatus(Job &job, pid_t pid, int wstatus) {
    assertm(pid != 0,
            "A pid=0 likely means a return from waitpid(...,WNOHANG) no "
            "signal was received from any child");

    pid_t pgid = job.pgid;

    if (pid == -1) {
        throw std::runtime_error(std::format("wait_job: waitpid({}): {}", -pgid,
                                             std::strerror(errno)));
    }

    if (!job.jobs.contains(pid)) {
        throw std::runtime_error(
            std::format("pid={} is not part of pgid={}", pid, pgid));
    }

    auto &stats = job.jobs.at(pid);

    if (WIFSTOPPED(wstatus)) {
        stats.stopped = true;
        std::println(stderr, "{}: stopped by {}({})", pid,
                     strsignal(WSTOPSIG(wstatus)), WSTOPSIG(wstatus));
        return;
    }

    stats.completed = true;

    if (WIFEXITED(wstatus)) {
        stats.exit_code = WEXITSTATUS(wstatus);
    } else if (WIFSIGNALED(wstatus)) {
        stats.exit_code = 1;
        stats.signaled = WTERMSIG(wstatus);

        std::println(stderr, "{}: Terminated by signal {}({})", pid,
                     strsignal(WTERMSIG(wstatus)), WTERMSIG(wstatus));
    }
}

Job Waiter::wait_job(Job &&job) {
    // TODO: this pgid, when executing in non-interctive shell, should be
    // equal for all the jobs
    pid_t pgid = job.pgid;

    assertm((pgid != 0) || (pgid == 0 && job.completed()),
            "A job with a pgid unitialized must be completed.");

    while (!job.completed() && !job.stopped()) {
        int wstatus;
        const pid_t pid = waitpid(-pgid, &wstatus, WUNTRACED);

        process_wstatus(job, pid, wstatus);
    }

    return job;
}

void Waiter::update_status(Job &job) {
    // TODO: this pgid, when executing in non-interctive shell, should
    // be equal for all the jobs
    pid_t pgid = job.pgid;

    assertm((pgid != 0) || (pgid == 0 && job.completed()),
            "A job with a pgid unitialized must be completed.");

    for (;;) {
        int wstatus;
        const pid_t pid = waitpid(-pgid, &wstatus, WUNTRACED | WNOHANG);

        if (pid == 0) {
            /* No processes ready to report.  */
            break;
        }

        if (pid == -1 && errno == ECHILD) {
            /* No processes ready to report.  */
            break;
        }

        process_wstatus(job, pid, wstatus);
    }
}

void Waiter::wait(Job &job) const {
    Job retval;

    if (shell.is_interactive) {
        /* Wait for it to report.  */
        retval = Waiter::wait_job(std::move(job));

        /* Put the shell back in the foreground.  */
        if (tcsetpgrp(shell.terminal, shell.pgid) == -1) {
            std::println(stderr, "tcsetpgrp({}, {}): {}", shell.terminal,
                         shell.pgid, std::strerror(errno));
        }

        /* Restore the shell’s terminal modes.  */
        job.set_modes(shell);
        // TODO: this is not fine when calling `stty`. Decide how do handle this
        tcsetattr(shell.terminal, TCSADRAIN, &shell.tmodes);
    } else {
        retval = Waiter::wait_job(std::move(job));
    }

    job = retval;
}

void Waiter::wait_inside_async(Job &job) const {
    pid_t pgid = job.pgid;

    /* Don't check for stopped jobs. An asyn list will terminate only when all
     * the childred are completed. Some of them might get stopped by the tty. We
     * don't want to loose them before exiting the async list.
     */
    while (!job.completed()) {
        int wstatus;
        const pid_t pid = waitpid(-pgid, &wstatus, WUNTRACED);

        process_wstatus(job, pid, wstatus);
    }
}

void Waiter::bg(Job &job) const {
    if (kill(-job.pgid, SIGCONT) < 0) {
        std::println(stderr, "kill({}, SIGCONG): {}", -job.pgid,
                     std::strerror(errno));
    }

    // TODO: error handling?
    job.mark_running();
}

void Waiter::fg(Job &job) const {
    /* Put the job in foreground */
    tcsetpgrp(shell.terminal, job.pgid);

    /* Restore the tmodes on the terminal before sening SIGCONT */
    job.restore_modes(shell);

    if (kill(-job.pgid, SIGCONT) < 0) {
        std::println(stderr, "kill({}, SIGCONT): {}", -job.pgid,
                     std::strerror(errno));
    }

    // TODO: error handling?
    job.mark_running();

    this->wait(job);
}

// ------------------------------------
// Spawner
// ------------------------------------

struct Spawner {
    CommandState state;
    const Shell &shell;
    SpawnType spawn_type = SpawnType::command;

  private:
    static void command_singnal(void) {
        /* Set the handling for job control signals back to the default.
         */
        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);
        signal(SIGTTIN, SIG_DFL);
        signal(SIGTTOU, SIG_DFL);
        signal(SIGCHLD, SIG_DFL);
    }

    static void subshell_singnal(void) {
        /* Set the handling for job control signals back to the default.
         * Subshell must ignore SIGTTIN, SIGTTOU, SIGTSTP
         */
        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        signal(SIGTSTP, SIG_IGN);
        signal(SIGTTIN, SIG_IGN);
        signal(SIGTTOU, SIG_IGN);
        signal(SIGCHLD, SIG_DFL);
    }

    static void async_signal(void) {
        /* Set the handling for job control signals back to the default.
         */
        signal(SIGINT, SIG_IGN);
        signal(SIGQUIT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);
        signal(SIGTTIN, SIG_IGN);
        signal(SIGTTOU, SIG_IGN);
        signal(SIGCHLD, SIG_DFL);
    }

  public:
    template <typename Fn, typename... Args>
    ExecStats spawn_async(Fn &&fn, Args &&...args) const {
        pid_t pgid = this->state.pipeline_pgid;

        const pid_t pid = fork();
        if (pid == -1) {
            perror("fork");
            exit(1);
        }

        if (pid == 0) {
            // -----------
            // Child
            // -----------

            if (shell.is_interactive) {
                /* Put the process into the process group and give the
                 * process group the terminal, if appropriate. This has to
                 * be done both by the shell and in the individual child
                 * processes because of potential race conditions.
                 */
                pid_t child_pid = getpid();
                pid_t child_pgid = (pgid != -1) ? pgid : child_pid;

                setpgid(child_pid, child_pgid);
                /* An async list cannot take control of the terminal.
                 */
                if (state.is_foreground &&
                    this->spawn_type != SpawnType::async_list) {

                    tcsetpgrp(shell.terminal, child_pgid);
                }

                /* Set properly the signal handlers.
                 * There is a difference if we are spwaning
                 * a subhsell or a simple command.
                 */
                switch (this->spawn_type) {
                case SpawnType::command:
                    this->command_singnal();
                    break;
                case SpawnType::subshell:
                    this->subshell_singnal();
                    break;
                case SpawnType::async_list:
                    this->async_signal();
                    break;
                }
            }

            std::invoke(std::forward<Fn>(fn), std::forward<Args>(args)...);
            exit(1);
        }

        // -----------
        // Parent
        // -----------

        // Process Group ID must be set from the parent as well to avoid
        // race conditions
        if (shell.is_interactive) {
            int retval{};
            pgid = (pgid != -1) ? pgid : pid;

            retval = setpgid(pid, pgid);
            if (retval == -1) {
                std::println(stderr, "{}: setpgid({}, {}): {}", __FUNCTION__,
                             pid, pgid, std::strerror(errno));
            }

            /* Put job in foreground */
            if (state.is_foreground &&
                this->spawn_type != SpawnType::async_list) {

                retval = tcsetpgrp(shell.terminal, pgid);
                if (retval == -1) {
                    std::println(stderr, "{}: tcsetpgrp({}, {}): {}",
                                 __FUNCTION__, shell.terminal, pgid,
                                 std::strerror(errno));
                }
            }
        } else {
            pgid = getpgrp();
        }

        // Don't wait for the child
        return ExecStats{
            .exit_code = 0,
            .child_pid = pid,
            .pipeline_pgid = pgid,
        };
    }
};

/**
 * @brief Create a pipe object
 *
 * @return std::optional<std::tuple<int, int>> tuple{reader_fd, writer_fd};
 */
static std::tuple<int, int> create_pipe() {
    int pipefd[2] = {-1, -1};
    const int retval = pipe(pipefd);

    if (retval == -1) {
        std::perror("pipe");
        exit(1);
    }

    return std::tuple{pipefd[0], pipefd[1]};
}

static bool is_builtin(const SimpleCommand &cmd) {
    const auto &prog = cmd.program;

    return prog == "bg" || prog == "cd" || prog == "exec" || prog == "exit" ||
           prog == "fg" || prog == "jobs";
}

std::optional<ExecStats> Executor::builtin(const SimpleCommand &cmd) {
    const auto &prog = cmd.program;
    int exit_code{};

    if (prog == "bg") {
        exit_code = builtin_bg(cmd, this->bg_jobs, Waiter(shell));
    } else if (prog == "cd") {
        exit_code = builtin_cd(cmd);
    } else if (prog == "exec") {
        exit_code = builtin_exec(cmd, this->shell);
    } else if (prog == "exit") {
        exit_code = builtin_exit(cmd);
    } else if (prog == "fg") {
        exit_code = builtin_fg(cmd, this->bg_jobs, Waiter(shell));
    } else if (prog == "jobs") {
        exit_code = builtin_jobs(cmd, this->bg_jobs);
    } else {
        return std::nullopt;
    }

    return ExecStats{
        .exit_code = exit_code,
        .child_pid = getpid(),
        // .pipeline_pgid =
        .completed = true,
    };
}

ExecStats Executor::simple_command(const SimpleCommand &cmd,
                                   const CommandState &state) {
    // Close the redirections used by the child, the parent no longer needs
    // them. The unneeded files will be automatically closed when the
    // destructor will be called.
    RedirectController redirect{state};
    Spawner spawner{
        .state = state,
        .shell = this->shell,
        .spawn_type = SpawnType::command,
    };

    if (!redirect.add_redirects(cmd.redirections)) {
        return ExecStats::ERROR;
    }

    // Check if a builtin can be run first before, before running
    // the program through exec().
    if (is_builtin(cmd)) {
        if (state.inside_pipeline) {
            return spawner.spawn_async(&Executor::builtin, this, cmd);
        } else {
            return this->builtin(cmd).value();
        }
    }

    auto child = [&]() {
        if (!redirect.apply_redirections())
            exit(1);

        Exec exec_prog{cmd, this->shell};
        exec_prog.exec();

        exit(1);
    };

    ExecStats retval = spawner.spawn_async(child);

    // std::println(stderr, "+ ({}, {}): {}", retval.child_pid,
    //              retval.pipeline_pgid, cmd.text());

    return retval;
}

// For substitution details take a look at the standard.
// https://pubs.opengroup.org/onlinepubs/9699919799/utilities/V3_chap02.html#tag_18_06_03
std::string Executor::cmdsub(const CmdSub &sub, const CommandState &state) {
    Job job{};

    Spawner spawner{
        .state = state,
        .shell = this->shell,
        .spawn_type = SpawnType::subshell,
    };

    // Setup piping for stdout redirections
    const auto [reader_fd, writer_fd] = create_pipe();

    auto child = [&]() {
        // -----------
        // Child
        // -----------

        close(reader_fd);
        dup2(writer_fd, STDOUT_FILENO);
        close(writer_fd);

        const auto stats = this->list(*sub.seq_list, state);

        exit(stats.last_stats.exit_code);
    };

    // -----------
    // Parent
    // -----------

    // Start child and read its output from reader_fd
    ExecStats child_stats = spawner.spawn_async(child);
    job.add(std::move(child_stats));

    // Start reading
    close(writer_fd);

    fd_streambuf buf{reader_fd};
    std::istream is(&buf);

    std::string substitution(std::istreambuf_iterator<char>(is), {});

    close(reader_fd);

    // Remove last newline if present
    if (!substitution.empty() && substitution.back() == '\n') {
        substitution = substitution.substr(0, substitution.size() - 1);
    }

    // The child should be already terminated, collect the signal
    // to avoid leaving zombie processes hanging around.
    Waiter{this->shell}.wait(job);

    return substitution;
}

ExecStats Executor::unsub_command(const UnsubCommand &cmd,
                                  const CommandState &state) {

    std::string program{};
    std::vector<std::string> arguments{};

    if (std::holds_alternative<CmdSub>(*cmd.program)) {
        program = this->cmdsub(std::get<CmdSub>(*cmd.program), state);
    } else {
        program = std::get<Token>(*cmd.program).text();
    }

    for (const auto &arg : cmd.arguments) {
        std::string arg_str;

        if (std::holds_alternative<CmdSub>(arg)) {
            arg_str = this->cmdsub(std::get<CmdSub>(arg), state);
        } else {
            arg_str = std::get<Token>(arg).text();
        }

        arguments.emplace_back(std::move(arg_str));
    }

    SimpleCommand expanded{
        .program = std::move(program),
        .arguments = std::move(arguments),
        .redirections = cmd.redirections,
        .envs = cmd.envs,
    };

    return this->simple_command(expanded, state);
}

static void add_shell_vars(Shell &shell, const SimpleAssignment &assign) {
    for (const auto &env : assign.envs) {
        shell.vars.upsert(env.whole.text(), std::nullopt);
    }
}

ExecStats Executor::simple_assignment(const SimpleAssignment &assign,
                                      const CommandState &state) {
    // Close the redirections used by the child, the parent no longer needs
    // them. The unneeded files will be automatically closed when the
    // destructor will be called.
    RedirectController redirect{state};
    Spawner spawner{state, this->shell};

    if (!redirect.add_redirects(assign.redirections)) {
        return ExecStats::ERROR;
    }

    if (state.inside_pipeline) {
        return spawner.spawn_async(add_shell_vars, this->shell, assign);
    }

    add_shell_vars(this->shell, assign);
    return ExecStats::shallow(getpid());
}

ExecStats Executor::and_list(const AndList &and_list,
                             const CommandState &state) {
    const auto lhs = this->op_list(*and_list.left, state);

    // JOB CONTROL:
    // Don't execute tht rhs if the lhs terminated with a SIGNINT signal
    auto sigint =
        lhs.signaled.transform([](int signal) { return signal == SIGINT; });
    if (sigint == true) {
        return lhs;
    }

    // TODO: hanldle if a process got stopped

    // Don't execute the rhs if the lhs terminated with an error
    if (lhs.exit_code != 0) {
        return lhs;
    }

    const auto rhs = this->op_list(*and_list.right, state);

    return rhs;
}

ExecStats Executor::or_list(const OrList &or_list, const CommandState &state) {
    const auto lhs = this->op_list(*or_list.left, state);

    // JOB CONTROL:
    // Don't execute tht rhs if the lhs terminated with a SIGNINT signal
    auto sigint =
        lhs.signaled.transform([](int signal) { return signal == SIGINT; });
    if (sigint == true) {
        return lhs;
    }

    // Don't execute the rhs if the lhs terminated with a success
    if (lhs.exit_code == 0) {
        return lhs;
    }

    const auto rhs = this->op_list(*or_list.right, state);

    return rhs;
}

Job Executor::pipeline(const Pipeline &pipeline, const CommandState &state) {
    assertm(!pipeline.cmds.empty(), "A pipeline must always contain something");

    Job job{};
    pid_t pipeline_pgid = state.pipeline_pgid;
    int prev_reader_fd = 0;

    auto no_cmds = std::max(pipeline.cmds.size() - 1, (size_t)0);
    for (const auto &cmd : pipeline.cmds | vw::take(no_cmds)) {
        auto pipefd = create_pipe();
        const auto [reader_fd, writer_fd] = pipefd;

        std::vector<std::tuple<int, int>> redirects{};
        if (prev_reader_fd != 0)
            redirects.emplace_back(STDIN_FILENO, prev_reader_fd);

        redirects.emplace_back(STDOUT_FILENO, writer_fd);
        prev_reader_fd = reader_fd;

        auto stats = this->command(cmd, {
                                            .redirects = std::move(redirects),
                                            .fd_to_close = {reader_fd},
                                            .inside_pipeline = true,
                                            .pipeline_pgid = pipeline_pgid,
                                        });

        pipeline_pgid = stats.pipeline_pgid;
        job.add(std::move(stats));
    }

    std::vector<std::tuple<int, int>> redirects{};
    if (prev_reader_fd != 0)
        redirects.emplace_back(STDIN_FILENO, prev_reader_fd);

    auto stats = this->command(pipeline.cmds.back(),
                               {
                                   .redirects = std::move(redirects),
                                   .fd_to_close = {},
                                   .inside_pipeline = false,
                                   .pipeline_pgid = pipeline_pgid,
                               });

    job.add(std::move(stats));

    return job;
}

ExecStats Executor::wait_pipeline(const Pipeline &pipeline,
                                  const CommandState &state) {
    auto job = this->pipeline(pipeline, state);
    Waiter{this->shell}.wait(job);

    auto stopped = job.stopped() && !job.completed();
    auto stats = job.exec_stats();

    if (stopped) {
        /* If a job was stpeed while waiting for it put it in the
         * background jobs
         */
        this->bg_jobs.emplace_back(std::move(job));
    }

    if (pipeline.negated /* && !stopped */) {
        stats.exit_code = (stats.exit_code != 0) ? 0 : 1;
    }

    return stats;
}

ExecStats Executor::op_list(const OpList &list, const CommandState &state) {
    const auto stats =
        std::visit(overloads{
                       [&](const AndList &and_list) {
                           return this->and_list(and_list, state);
                       },
                       [&](const OrList &or_list) {
                           return this->or_list(or_list, state);
                       },
                       [&](const Pipeline &pipeline) {
                           return this->wait_pipeline(pipeline, state);
                       },
                   },
                   list);

    return stats;
}

ListStats Executor::sequential_list(const SequentialList &sequential_list,
                                    const CommandState &state) {

    ListStats stats{};

    if (sequential_list.left.has_value()) {
        stats = this->list(**sequential_list.left, state);
    }

    stats.last_stats = this->op_list(*sequential_list.right, state);

    return stats;
}

ListStats Executor::async_list(const AsyncList &async_list,
                               const CommandState &state) {
    ListStats stats{};

    if (async_list.left.has_value()) {
        stats = this->list(**async_list.left, state);
    }

    Spawner spawner{.state = state,
                    .shell = this->shell,
                    .spawn_type = SpawnType::async_list};

    const auto async_fn = [&]() {
        Waiter waiter{shell};
        CommandState async_state{state};
        async_state.pipeline_pgid = getpgrp();
        async_state.is_foreground = false;

        /* Clear any bg_jobs from the parent. It's ok to do this in this context
         * because the child will have its own memory space (due to the COW
         * mechanism). Use the background jobs to wait on any child if it get
         * stopped. e.g. `cat &` should be stopped by a SIGTTIN when trying to
         * read the stdin from background.
         */
        this->bg_jobs.clear();

        const auto stats = this->op_list(*async_list.right, async_state);

        /* Wait for any background job before terminating
         */
        while (!this->bg_jobs.empty()) {
            for (auto &job : this->bg_jobs)
                waiter.wait_inside_async(job);

            this->bg_jobs =
                this->bg_jobs |
                vw::filter([](const auto &j) { return !j.completed(); }) |
                std::ranges::to<std::vector>();
        }

        exit(stats.exit_code);
    };

    /* A job must be created from a async list. This will represent
     * the background process in the main shell.
     */
    Job job{};

    auto async_stats = spawner.spawn_async(async_fn);

    std::println(stderr, "{}: Background {:?}", async_stats.child_pid,
                 async_stats);

    job.add(std::move(async_stats));

    stats.last_stats = job.exec_stats();
    stats.bg_jobs.emplace_back(std::move(job));

    return stats;
}

ListStats Executor::list(const List &list, const CommandState &state) {
    auto stats = std::visit(overloads{
                                [&](const SequentialList &seq) {
                                    return this->sequential_list(seq, state);
                                },
                                [&](const AsyncList &async) {
                                    return this->async_list(async, state);
                                },
                            },
                            list);

    return stats;
}

ExecStats Executor::command(const Command &command, const CommandState &state) {
    const auto stats =
        std::visit(overloads{
                       [&](const UnsubCommand &cmd) {
                           return this->unsub_command(cmd, state);
                       },
                       [&](const SimpleAssignment &assign) {
                           return this->simple_assignment(assign, state);
                       },
                       [&](const Subshell &subshell) {
                           return this->subshell(subshell, state);
                       },
                   },
                   command);

    return stats;
}

ExecStats Executor::subshell(const Subshell &subshell,
                             const CommandState &state) {
    // Close the redirections used by the child, the parent no longer needs
    // them. The unneeded files will be automatically closed when the
    // destructor will be called.
    RedirectController redirect{state};
    Spawner spawner{state, this->shell, SpawnType::subshell};

    if (!redirect.add_redirects(subshell.redirections)) {
        return {.exit_code = 1};
    }

    auto subshell_call = [&]() {
        if (!redirect.apply_redirections())
            exit(1);

        const auto child_status = this->list(
            *subshell.seq_list, {.pipeline_pgid = state.pipeline_pgid});

        exit(child_status.last_stats.exit_code);
    };

    ExecStats retval = spawner.spawn_async(subshell_call);

    return retval;
}

ExecStats Executor::program(const ThisProgram &program) {
    if (program.child.empty())
        return {};

    ExecStats retval{};
    for (const auto &complete_command : program.child) {
        auto list_stats = this->list(complete_command, {});

        this->bg_jobs.append_range(list_stats.bg_jobs);

        /* The returned stats are always the one of the last list runned.
         */
        retval = list_stats.last_stats;
    }

    return retval;
}

bool Executor::line_has_continuation() const {
    assert(!this->input_buffer.empty());

    UnbufferedTokenizer tokenizer{this->input_buffer.back()};

    TokenType prev = TokenType::eof;
    TokenType next =
        tokenizer.next_token()
            .transform([](const Token &&token) { return token.type; })
            .value_or(TokenType::eof);

    while (next != TokenType::eof && next != TokenType::new_line) {
        prev = next;

        next = tokenizer.next_token()
                   .transform([](const Token &token) { return token.type; })
                   .value_or(TokenType::eof);
    }

    return prev == TokenType::line_continuation || prev == TokenType::and_and ||
           prev == TokenType::or_or || prev == TokenType::pipe;
}

bool Executor::read_stdin() {
    std::string new_line;
    std::getline(std::cin, new_line);

    if (!std::cin) {
        // TODO: check if we are actually at eof
        // if (std::cin.eof())

        return false;
    }

    new_line += "\n";
    this->input_buffer.emplace_back(std::move(new_line));

    std::println(stderr, "line: {}", new_line);

    return true;
}

std::vector<std::string> Executor::process_input() {
    // Create a support buffer where the line_continuations are cut
    // and the subsequent lines are pasted togheter
    std::vector<std::string> support;

    // TODO: move in its own logic
    for (const auto &line : this->input_buffer) {
        if (support.empty()) {
            support.emplace_back(line);
            continue;
        }

        std::string &last = support.back();
        std::string end = last.substr(std::max<size_t>(0, last.size() - 2));

        if (end == "\\\n")
            last = last.substr(0, last.size() - 2) + line;
        else
            support.emplace_back(line);
    }

    return support;
}

ExecStats Executor::execute() {
    auto support = this->process_input();

    Tokenizer tokenizer{support};
    SyntaxTree<Tokenizer> tree;

    // TODO: modify this
    if (tokenizer.next_is_eof())
        return {};

    const auto program = tree.program(tokenizer);

    if (!program.has_value())
        throw std::runtime_error("Parsing failed!");

    std::println(stderr, "=== SYNTAX TREE ===");
    std::println(stderr, "{:#?}", *program);

    std::println(stderr, "=== COMMAND BEGIN ===");

    const auto retval = this->program(*program);

    return retval;
}

TerminalState Executor::update() {

    if (!this->read_stdin()) {
        return TerminalState{
            .terminate_session = true,
        };
    }

    if (this->line_has_continuation()) {
        return {.needs_more = true};
    }

    const auto exec_stats = this->execute();
    this->input_buffer.clear();

    return {
        .exit_code = exec_stats.exit_code,
    };
}

void Executor::loop() {
    TerminalState state{};

    while (true) {
        if (state.terminate_session) {
            break;
        }

        if (this->shell.is_interactive) {
            if (state.exit_code != 0)
                std::print(red);

            if (state.needs_more) {
                std::print("> ");
            } else {
                // TODO: move from here
                for (auto &job : this->bg_jobs) {
                    Waiter::update_status(job);

                    if (job.completed()) {
                        std::println(stderr, "{}: Completed stats={:?}",
                                     job.job_master, job.exec_stats());
                    }
                }

                /* Remove completed jobs */
                this->bg_jobs = this->bg_jobs | vw::filter([](const auto &job) {
                                    return !job.completed();
                                }) |
                                std::ranges::to<std::vector>();

                std::print("$ ");
            }

            std::print(reset);
            std::cout << std::flush;
        }

        state = this->update();

        std::println(stderr, "terminate: {}", state.terminate_session);
    }
}
