# testsh

This project is based on the guide <https://github.com/tokenrove/build-your-own-shell>

Run the project:

```shell
bazel run :testsh
```

Optimized build:

```shell
bazel build --config=opt :testsh
```

Debug the project:

Run the VSCode task: `Build Testsh (Debug)` from the file `./.vscode/tasks.json` and then run the VSCode debugger.

## Stages

1. [x] Stage 1: lists, and, or, subshell, line continuations, cd, exec, exit
1. [x] Stage 2: redirections, pipes
1. [ ] Stage 3: job control system

## Links

- Job Control Sytem: <https://www.gnu.org/software/libc/manual/html_node/Implementing-a-Shell.html>
