# myTinySQL - build with `make`, run the tests with `make test`, make the HTML docs with `make doxygen`.

CXX      ?= g++
CXXFLAGS ?= -std=c++20 -Wall -Wextra -Wpedantic -O2 -pthread
# Headers (.hpp) live in include/, sources (.cpp) in src/.
CPPFLAGS += -Iinclude

SRC := $(wildcard src/*.cpp)
OBJ := $(SRC:src/%.cpp=build/%.o)

myTinySql: $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^

build/%.o: src/%.cpp | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

build:
	mkdir -p build

test: myTinySql
	tests/run_tests.sh

# HTML documentation from the Doxygen comments, in build/doxygen/html/.
doxygen:
	mkdir -p build/doxygen
	PROJECT_NUMBER="$$(git describe --tags --always 2>/dev/null)" doxygen Doxyfile
	@echo "Open build/doxygen/html/index.html"

clean:
	rm -rf build myTinySql

.PHONY: test clean doxygen

-include $(OBJ:.o=.d)
