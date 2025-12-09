#include "job.h"

bool Job::completed() const {
    for (const auto &entry : this->jobs) {
        if (!entry.second.completed)
            return false;
    }

    return true;
}

void Job::add(ExecStats &&stats) {
    assertm((stats.pipeline_pgid == -1) ? (stats.completed == true) : true,
            "A builtin is run immediatelly and not inside a child if it's not "
            "inside a pipiline. Only builtins can have pgid=-1 and "
            "completed=true.");

    if (this->pgid == 0 && stats.pipeline_pgid != -1)
        this->pgid = stats.pipeline_pgid;

    this->jobs[stats.child_pid] = std::move(stats);
    this->job_master = stats.child_pid;
}

ExecStats Job::exec_stats() const { return this->jobs.at(this->job_master); }
