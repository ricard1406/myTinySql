# myTinySQL - build with `make`, run the tests with `make test`.

CXX      ?= g++
CXXFLAGS ?= -std=c++20 -Wall -Wextra -Wpedantic -O2 -pthread

SRC := $(wildcard src/*.cpp)
OBJ := $(SRC:src/%.cpp=build/%.o)

myTinySql: $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^

build/%.o: src/%.cpp | build
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

build:
	mkdir -p build

test: myTinySql
	tests/run_tests.sh

clean:
	rm -rf build myTinySql

.PHONY: test clean

-include $(OBJ:.o=.d)
