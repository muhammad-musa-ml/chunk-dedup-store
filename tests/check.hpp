#pragma once
#include <atomic>
#include <cstdio>
#include <utility>
#include <vector>

namespace check {

using TestList = std::vector<std::pair<const char*, void (*)()>>;

inline TestList& tests() {
    static TestList t;
    return t;
}
inline std::atomic<int>& failures() {
    static std::atomic<int> f{0};
    return f;
}
inline std::atomic<int>& checks() {
    static std::atomic<int> c{0};
    return c;
}
struct Reg {
    Reg(const char* name, void (*fn)()) { tests().push_back({name, fn}); }
};

}  // namespace check

#define CHECK(cond)                                                              \
    do {                                                                         \
        ++check::checks();                                                       \
        if (!(cond)) {                                                           \
            ++check::failures();                                                 \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                        \
    } while (0)

#define TEST(name)                                       \
    static void name();                                  \
    static check::Reg reg_##name(#name, name);           \
    static void name()
