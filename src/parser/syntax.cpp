#include "syntax.h"
#include <print>

SyntaxTree::SyntaxTree(std::string_view input) : tokenizer(input) {}

Program SyntaxTree::build()
{
    const auto program{this->tokenizer.next_token().value()};
    std::vector<std::string_view> args{};

    while (const auto arg = this->tokenizer.peek())
    {
        if (arg->type == TokenType::separator)
        {
            this->tokenizer.next_token();
            continue;
        }

        if (arg->type != TokenType::word)
            break;

        args.emplace_back(arg->value);

        this->tokenizer.next_token();
    }

    return Program{program.value, std::move(args)};
}
