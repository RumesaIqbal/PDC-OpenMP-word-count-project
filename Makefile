CC ?= gcc
CFLAGS ?= -O2 -std=c11 -fopenmp -Wall -Wextra -Iinclude
PY ?= py

ifeq ($(OS),Windows_NT)
EXT := .exe
LDLIBS := -lpsapi
else
EXT :=
LDLIBS :=
endif

BUILD := build
HEADERS := $(wildcard include/*.h)

SEQ := $(BUILD)/wc_seq$(EXT)
PAR := $(BUILD)/wc_par$(EXT)
VERIFY := $(BUILD)/wc_verify$(EXT)

.PHONY: all clean test preprocess sizes bench

all: $(SEQ) $(PAR) $(VERIFY)

$(BUILD):
	mkdir -p $(BUILD)

$(SEQ): src/seq_main.c src/wc.c $(HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) src/seq_main.c src/wc.c -o $@ $(LDLIBS)

$(PAR): src/par_main.c src/wc.c $(HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) src/par_main.c src/wc.c -o $@ $(LDLIBS)

$(VERIFY): src/verify_main.c | $(BUILD)
	$(CC) $(CFLAGS) $< -o $@ $(LDLIBS)

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
