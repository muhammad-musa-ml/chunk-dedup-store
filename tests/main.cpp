#include <cstdio>
#include <cstring>

#include "check.hpp"

// Usage: tests [substring]. With a substring, only tests whose name contains it run.
int main(int argc, char** argv) {
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int ran = 0;
    for (auto& [name, fn] : check::tests()) {
        if (filter && !std::strstr(name, filter)) continue;
        int before = check::failures();
        fn();
        ++ran;
        std::printf("%-44s %s\n", name, check::failures() == before ? "ok" : "FAILED");
    }
    std::printf("%d tests, %d checks, %d failures\n", ran, check::checks().load(),
                check::failures().load());
    return (ran == 0 || check::failures() != 0) ? 1 : 0;
}
