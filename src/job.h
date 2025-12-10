#ifndef TESTSH_JOB_H
#define TESTSH_JOB_H

#include "util.h"
#include <format>
#include <optional>
#include <sys/types.h>
#include <unordered_map>

struct ExecStats {
    int exit_code;
    pid_t child_pid = -1;
    pid_t pipeline_pgid = -1;
    bool completed = false;
    bool stopped = false;
    bool in_background = false;
    std::optional<int> signaled = std::nullopt;

    static ExecStats ERROR;
};

/**
 * This struct represent an instance of a job, composed of many processes.
 *
 * For more info on implementing a job controls system take a look at:
 * https://www.gnu.org/software/libc/manual/html_node/Implementing-a-Shell.html
 *
 */
struct Job {
    pid_t pgid;
    std::unordered_map<pid_t, ExecStats> jobs;
    pid_t job_master;

    bool completed() const;

    bool stopped() const;

    void add(ExecStats &&stats);

    ExecStats exec_stats() const;
};

// -------------------------------------
// Format
// -------------------------------------

template <> struct std::formatter<ExecStats> : debug_spec {
    auto format(const ExecStats &e, auto &ctx) const {
        this->start<ExecStats>(ctx);
        this->field("exit_code", e.exit_code, ctx);
        this->field("child_pid", e.child_pid, ctx);
        this->field("pipeline_pgid", e.pipeline_pgid, ctx);
        this->field("completed", e.completed, ctx);
        this->field("stopped", e.stopped, ctx);
        this->field("signaled", e.signaled, ctx);
        return this->finish(ctx);
    }
};

template <> struct std::formatter<Job> : debug_spec {
    auto format(const Job &j, auto &ctx) const {
        std::vector<ExecStats> jobs;
        for (const auto &[key, value] : j.jobs) {
            jobs.emplace_back(value);
        }

        this->start<Job>(ctx);
        this->field("pgid", j.pgid, ctx);
        this->field("jobs", jobs, ctx);
        this->field("job_master", j.job_master, ctx);
        return this->finish(ctx);
    }
};

#endif // TESTSH_JOB_H
