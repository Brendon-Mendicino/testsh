#include "job.h"
#include "shell.h"
#include <optional>

ExecStats ExecStats::ERROR = {
    .exit_code = 1,
    .child_pid = -1,
    .pipeline_pgid = -1,
    .completed = true,
    .stopped = false,
    .in_background = false,
    .signaled = std::nullopt,
};

ExecStats ExecStats::SHALLOW = {
    .exit_code = 0,
    .child_pid = 0,
    .pipeline_pgid = -1,
    .completed = true,
    .stopped = false,
    .in_background = false,
    .signaled = std::nullopt,
};

ExecStats ExecStats::shallow(pid_t pid) {
    auto data = SHALLOW;
    data.child_pid = pid;
    return data;
}

bool Job::completed() const {
    for (const auto &entry : this->jobs) {
        if (!entry.second.completed)
            return false;
    }

    return true;
}

bool Job::stopped() const {
    for (const auto &entry : this->jobs) {
        if (!entry.second.completed && !entry.second.stopped)
            return false;
    }

    return true;
}

void Job::mark_running() {
    for (auto &[_, prog] : this->jobs) {
        prog.stopped = false;
    }
}

void Job::add(ExecStats &&stats) {
    assertm(
        (stats.pipeline_pgid == -1) ? (stats.completed == true) : true,
        "If a command added to job has pgig=-1 it must be compleded. The "
        "reason why a command might have pgid=-1 could be because a builtin "
        "was run or something caused an error before the command could be "
        "run.");

    if (this->pgid == 0 && stats.pipeline_pgid != -1)
        this->pgid = stats.pipeline_pgid;

    this->jobs[stats.child_pid] = std::move(stats);
    this->job_master = stats.child_pid;
}

ExecStats Job::exec_stats() const { return this->jobs.at(this->job_master); }

void Job::set_modes(const Shell &shell) {
    tcgetattr(shell.terminal, &tmodes);
    tmodes_init = true;
}

void Job::restore_modes(const Shell &shell) {
    if (tmodes_init) {
        /* Restore terminal state the previous state of the job.
         */
        tcsetattr(shell.terminal, TCSADRAIN, &tmodes);
    } else {
        /* If the job terminal modes where never initialized it means that the
         * job started in background. This means that it never modified,
         * just assign the current ones without doing anything.
         */
        tcgetattr(shell.terminal, &tmodes);
        tmodes_init = true;
    }
}
