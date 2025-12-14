# testsh

This project is based on the guide <https://github.com/tokenrove/build-your-own-shell>

Run the project:

```sh
bazel run :testsh
```

Optimized build:

```sh
bazel build --config=opt :testsh
```

Debug the project:

Run the VSCode task: `Build Testsh (Debug)` from the file `./.vscode/tasks.json` and then run the VSCode debugger.

## Generate `compile_commands.json`

`compile_commands.json` is needed by `clangd` to properly do code highlighting/completions with the bazel dependencies.

```sh
BUILD='build/compile_commands'
URL='https://github.com/kiron1/bazel-compile-commands/releases/download/v0.20.1/bazel-compile-commands_0.20.1-linux_amd64.zip'
mkdir -p "$BUILD"
$(cd "$BUILD" && \
    FILE=$(basename "$URL") && \
    curl -L -o "$FILE" "$URL" && \
    unzip -q "$FILE")

"$BUILD"/usr/bin/bazel-compile-commands
```

## Stages

1. [x] Stage 1: lists, and, or, subshell, line continuations, cd, exec, exit
1. [x] Stage 2: redirections, pipes
1. [x] Stage 3: job control system
1. [ ] Stage 4: envs

## Links

- Job Control Sytem: <https://www.gnu.org/software/libc/manual/html_node/Implementing-a-Shell.html>
