#ifndef TESTSH_EXECUTOR_H
#define TESTSH_EXECUTOR_H

#include "tokenizer.h"
#include <string_view>
#include <vector>

class Executor {
    Tokenizer tokenizer;

    public:
        Executor(std::string_view input);

        Program execute();
};


#endif // TESTSH_EXECUTOR_H