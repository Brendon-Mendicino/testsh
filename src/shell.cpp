#include "shell.h"
#include <csignal>
#include <cstdio>
#include <optional>
#include <unistd.h>
#include <unordered_set>
#include <utility>

// ------------------------------------
// Var
// ------------------------------------

std::string_view Var::name() const {
    std::string_view view{this->str};

    return view.substr(0, this->eq_off);
}

std::string_view Var::value() const {
    std::string_view view{this->str};

    return view.substr(this->eq_off + std::string_view{"="}.size());
}

// ------------------------------------
// ShellVars
// ------------------------------------

void ShellVars::upsert(std::string var, std::optional<VarAttr> attr) {
    auto eq_off = var.find("=");

    assertm(eq_off != 0 && eq_off != std::string::npos,
            "A string passed to upsert must already be in a valid environment "
            "shell format!");

    Var shell_var{
        .str = std::move(var),
        .eq_off = eq_off,
        .attr = {},
    };

    auto it = this->vars.find(shell_var);
    if (it != this->vars.end()) {
        if (!attr)
            attr.emplace(it->attr);

        this->vars.erase(it);
    }

    if (attr) {
        shell_var.attr = take(attr);
    }

    this->vars.insert(std::move(shell_var));
}

static void init_environment(ShellVars &vars) {
    for (size_t i = 0; environ[i] != nullptr; i++) {
        vars.upsert(environ[i], VarAttr{.external = true});
    }
}

/* Make sure the shell is running interactively as the foreground job
   before proceeding. */
Shell::Shell() : pgid(), tmodes(), terminal(), is_interactive(), vars() {
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

    init_environment(this->vars);
}
