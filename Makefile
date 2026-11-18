CC      = gcc
CFLAGS  ?= -std=c11 -O2 -Wall -Wextra -Wpedantic
CPPFLAGS ?= -Isrc
LDLIBS  ?= -lm

TEST_BIN := build/test_resample
SOURCES  := src/resample.c tests/test_resample.c
HEADERS  := src/resample.h

.PHONY: all test clean

all: $(TEST_BIN)

$(TEST_BIN): $(SOURCES) $(HEADERS)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(SOURCES) $(LDLIBS)

test: $(TEST_BIN)
	./$(TEST_BIN)

clean:
	rm -rf build
