#include "job.h"

bool Job::completed() const
{
    for (const auto &entry : this->jobs)
    {
        if (!entry.second.completed)
            return false;
    }

    return true;
}

void Job::add(ExecStats &&stats)
{
    if (this->pgid == 0)
        this->pgid = stats.pipeline_pgid;

    this->jobs[stats.child_pid] = std::move(stats);
    this->job_master = stats.child_pid;
}

ExecStats Job::exec_stats() const
{
    return this->jobs.at(this->job_master);
}