#include "shell.h"
#include <csignal>
#include <cstdio>
#include <unistd.h>

/* Make sure the shell is running interactively as the foreground job
   before proceeding. */
Shell::Shell() : pgid(), tmodes(), terminal(), is_interactive() {
    /* See if we are running interactively.  */
    terminal = STDIN_FILENO;
    is_interactive = isatty(terminal);

    if (is_interactive) {
        /* Loop until we are in the foreground.  */
        while (tcgetpgrp(terminal) != (pgid = getpgrp()))
            kill(-pgid, SIGTTIN);

        /* Ignore interactive and job-control signals.  */
        signal(SIGINT, SIG_IGN);
        signal(SIGQUIT, SIG_IGN);
        signal(SIGTSTP, SIG_IGN);
        signal(SIGTTIN, SIG_IGN);
        signal(SIGTTOU, SIG_IGN);
        // TODO: check how to handle SIGCHLD
        // signal(SIGCHLD, SIG_IGN);

        /* Put ourselves in our own process group.  */
        pgid = getpid();
        if (setpgid(pgid, pgid) < 0) {
            perror("Couldn't put the shell in its own process group");
            pgid = getpgrp();
            // exit(1);
        }

        /* Grab control of the terminal.  */
        tcsetpgrp(terminal, pgid);

        /* Save default terminal attributes for shell.  */
        tcgetattr(terminal, &tmodes);
    }
}
