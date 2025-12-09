#ifndef TESTSH_SHELL_H
#define TESTSH_SHELL_H

#include "util.h"
#include <format>
#include <termios.h>
#include <unistd.h>

struct Shell {
    pid_t pgid;
    termios tmodes;
    int terminal;
    int is_interactive;

    Shell();
};

// ------------------------------------
// FORMATTER
// ------------------------------------

template <typename CharT> struct std::formatter<Shell, CharT> : debug_spec {
    auto format(const Shell &s, auto &ctx) const {
        this->start<Shell>(ctx);
        this->field("pgid", s.pgid, ctx);
        this->field("terminal", s.terminal, ctx);
        this->field("is_interactive", s.is_interactive, ctx);
        return this->finish(ctx);
    }
};

#endif // TESTSH_SHELL_H
