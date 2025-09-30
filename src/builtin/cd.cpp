//
// Created by brendon on 04/09/2025.
//

#include "cd.h"
#include <filesystem>

namespace fs = std::filesystem;

void cd(const fs::path& path) {
    std::error_code error_code{};

    fs::current_path(path, error_code);

    // if (error_code)
}