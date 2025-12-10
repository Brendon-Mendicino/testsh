#include "job.h"

ExecStats ExecStats::ERROR = {
    .exit_code = 1,
    .child_pid = -1,
    .pipeline_pgid = -1,
    .completed = true,
    .stopped = false,
};

bool Job::completed() const {
    for (const auto &entry : this->jobs) {
        if (!entry.second.completed)
            return false;
    }

    return true;
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
