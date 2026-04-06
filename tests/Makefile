# Native test build — uses host g++, not the ARM cross-compiler.
# Run from repo root:  make -C tests
#              or from tests/:  make

CXX      = g++
CXXFLAGS = -std=c++14 -Wall -Wextra -g -O0

TESTS = test_pause_core test_launcher_core_map test_launcher_io test_launcher_covers

.PHONY: all clean run

all: $(TESTS)

test_pause_core: test_pause_core.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

test_launcher_core_map: test_launcher_core_map.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

test_launcher_io: test_launcher_io.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

test_launcher_covers: test_launcher_covers.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

run: all
	@for t in $(TESTS); do echo "--- $$t ---"; ./$$t || exit 1; done
	@echo "\nAll test suites passed."

clean:
	rm -f $(TESTS)
