.RECIPEPREFIX = >
CXX ?= g++
WARN = -Wall -Wextra -Wpedantic -Wshadow
CXXFLAGS = -std=c++20 -O2 -g $(WARN) -Isrc -pthread
SRC := $(wildcard src/*.cpp)
TESTSRC := $(wildcard tests/*.cpp)

.PHONY: test tsan bench mutants clean

test: build/tests
> build/tests

# ThreadSanitizer needs ASLR off under WSL2, hence setarch -R.
tsan: build/tests_tsan
> setarch -R build/tests_tsan concurrent

bench: build/bench
> build/bench

# Breaks one thing at a time in copies under build/mut and checks that the tests notice.
mutants:
> python3 tools/mutate.py . build/mut
> bash tools/run_mutants.sh build/mut

build/tests: $(SRC) $(TESTSRC) $(wildcard src/*.hpp tests/*.hpp)
> mkdir -p build
> $(CXX) $(CXXFLAGS) $(SRC) $(TESTSRC) -o $@

build/tests_tsan: $(SRC) $(TESTSRC) $(wildcard src/*.hpp tests/*.hpp)
> mkdir -p build
> $(CXX) -std=c++20 -O1 -g $(WARN) -Isrc -pthread -fsanitize=thread $(SRC) $(TESTSRC) -o $@

build/bench: $(SRC) bench/bench.cpp $(wildcard src/*.hpp)
> mkdir -p build
> $(CXX) $(CXXFLAGS) $(SRC) bench/bench.cpp -o $@

clean:
> rm -rf build
