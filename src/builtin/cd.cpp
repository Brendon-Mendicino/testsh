//
// Created by brendon on 04/09/2025.
//

#include "cd.h"
#include <filesystem>
#include <print>
#include <cstdio>

namespace fs = std::filesystem;

int builtin_cd(const std::vector<std::string_view> &args)
{
    fs::path target{};

    if (args.empty() || args[0] == "~")
    {
        const char *home = std::getenv("HOME");

        if (home == nullptr)
        {
            std::println(stderr, "cd: $HOME not set");
            return 1;
        }

        target = home;
    }
    else if (args.size() == 1)
    {
        target = args[0];
    }
    else
    {
        std::println(stderr, "cd: too many arguments");
        return 1;
    }

    // Change current path in the application
    std::error_code ec;
    fs::current_path(target, ec);

    if (ec)
    {
        std::println(stderr, "cd: {}: {}", target.string(), ec.message());
        return 1;
    }

    return 0;
}