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
    ExecStats and_list(const AndList &and_list);
    ExecStats or_list(const OrList &or_list);
    ExecStats words(const Words &words);
    ExecStats op_list(const OpList &list);
    ExecStats sequential_list(const SequentialList &sequential_list);

public:
    Executor(std::string_view input);

    ExecStats execute();
};

#endif // TESTSH_EXECUTOR_H