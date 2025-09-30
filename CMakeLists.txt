cmake_minimum_required(VERSION 4.0)
project(testsh)

set(CMAKE_CXX_STANDARD 26)

add_executable(testsh src/main.cpp
        src/builtin/cd.cpp
        src/builtin/cd.h
)
