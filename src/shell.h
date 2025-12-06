#ifndef TESTSH_SHELL_H
#define TESTSH_SHELL_H

#include <unistd.h>
#include <termios.h>

struct Shell
{
    pid_t pgid;
    termios tmodes;
    int terminal;
    int is_interactive;

    Shell();
};



#endif // TESTSH_SHELL_H