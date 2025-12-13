#ifndef TESTSH_BUILTIN_H
#define TESTSH_BUILTIN_H

#include "executor.h"
#include "job.h"
#include "shell.h"
#include "syntax.h"
#include <vector>

int builtin_bg(const SimpleCommand &bg, std::vector<Job> &jobs,
               const Waiter &waiter);

int builtin_cd(const SimpleCommand &cd);

int builtin_exec(const SimpleCommand &exec, const Shell &shell);

int builtin_exit(const SimpleCommand &exit);

int builtin_fg(const SimpleCommand &fg, std::vector<Job> &jobs,
               const Waiter &waiter);

int builtin_jobs(const SimpleCommand &jobs, const std::vector<Job> &bg_jobs);

#endif // TESTSH_BUILTIN_H
