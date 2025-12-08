#ifndef TESTSH_BUILTIN_H
#define TESTSH_BUILTIN_H

#include "syntax.h"
#include <string_view>
#include <vector>

// TODO: modify
int builtin_cd(const SimpleCommand &cd);

int builtin_exec(const SimpleCommand &exec);

int builtin_exit(const SimpleCommand &exit);

#endif // TESTSH_BUILTIN_H
