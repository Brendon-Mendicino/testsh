#include "executor.h"
#include <print>
#include <sys/wait.h>
#include <unistd.h>

static void loop(void) {
    Executor executor{};

    std::println(stderr, "testsh pid: {}, shell: {:#?}", getpid(),
                 executor.shell);

    executor.loop();
}

int main() {
    loop();

    return 0;
}
