#ifndef TESTSH_EXECUTOR_H
#define TESTSH_EXECUTOR_H

#include "tokenizer.h"
#include "syntax.h"
#include <string_view>
#include <vector>

struct ExecStats
{
    int exit_code;
};

class Executor
{
    std::string_view input;

    ExecStats execute_program(const Program &prog);
    ExecStats negate(const Program &prog);

public:
    Executor(std::string_view input);

    ExecStats execute();
};

#endif // TESTSH_EXECUTOR_H