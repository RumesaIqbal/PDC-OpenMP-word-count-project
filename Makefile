CXX ?= g++
CXXFLAGS ?= -O2 -std=c++17 -fopenmp -Wall -Wextra -Iinclude
PY ?= py

ifeq ($(OS),Windows_NT)
EXT := .exe
LDLIBS := -lpsapi
else
EXT :=
LDLIBS :=
endif

BUILD := build
HEADERS := $(wildcard include/*.hpp)

SEQ := $(BUILD)/wc_seq$(EXT)
PAR := $(BUILD)/wc_par$(EXT)
VERIFY := $(BUILD)/wc_verify$(EXT)

.PHONY: all clean test preprocess sizes bench

all: $(SEQ) $(PAR) $(VERIFY)

$(BUILD):
	mkdir -p $(BUILD)

$(SEQ): src/seq_main.cpp $(HEADERS) | $(BUILD)
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDLIBS)

$(PAR): src/par_main.cpp $(HEADERS) | $(BUILD)
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDLIBS)

$(VERIFY): src/verify_main.cpp $(HEADERS) | $(BUILD)
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDLIBS)

test: all
	$(PY) tests/run_tests.py

preprocess:
	$(PY) tools/preprocess.py

sizes:
	$(PY) tools/make_sizes.py

bench:
	$(PY) tools/bench.py

clean:
	rm -rf $(BUILD)
