#ifndef TESTSH_BUILTIN_H
#define TESTSH_BUILTIN_H

#include "../parser/syntax.h"
#include <vector>
#include <string_view>


// TODO: modify
int builtin_cd(const std::vector<std::string_view> &args);

int builtin_exec(const Program &exec);

int builtin_exit(const Program &exit);

#endif // TESTSH_BUILTIN_H