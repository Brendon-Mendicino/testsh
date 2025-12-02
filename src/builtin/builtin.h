#ifndef TESTSH_BUILTIN_H
#define TESTSH_BUILTIN_H

#include "../parser/syntax.h"
#include <vector>
#include <string_view>


// TODO: modify
int builtin_cd(const SimpleCommand &cd);

int builtin_exec(const SimpleCommand &exec);

int builtin_exit(const SimpleCommand &exit);

#endif // TESTSH_BUILTIN_H