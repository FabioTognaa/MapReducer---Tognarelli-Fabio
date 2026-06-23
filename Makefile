CC      = gcc
CFLAGS  = -std=c11 -pthread -Wall -Wextra
CPPFLAGS = -Iinclude
AR      = ar
ARFLAGS = rcs

LIBMR      = libmr.a
LIBMR_OBJS = src/mr.o src/io.o src/log.o src/input.o src/queue.o src/mapper_proc.o

EXAMPLE     = examples/minimal
EXAMPLE_SRC = examples/minimal.c

TEST_LOG      = tests/log
TEST_LOG_SRC  = tests/log.c
TEST_INPUT    = tests/input
TEST_INPUT_SRC = tests/input.c
TEST_MAPPER   = tests/mapper
TEST_MAPPER_SRC = tests/mapper.c

.PHONY: all test clean

all: $(LIBMR) $(EXAMPLE)

$(LIBMR): $(LIBMR_OBJS)
	$(AR) $(ARFLAGS) $@ $^

$(EXAMPLE): $(EXAMPLE_SRC) $(LIBMR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $(EXAMPLE_SRC) $(LIBMR) -pthread

src/%.o: src/%.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

test: $(LIBMR) $(TEST_LOG) $(TEST_INPUT) $(TEST_MAPPER)
	./$(TEST_LOG)
	./$(TEST_INPUT)
	./$(TEST_MAPPER)

$(TEST_LOG): $(TEST_LOG_SRC) $(LIBMR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $(TEST_LOG_SRC) $(LIBMR) -pthread

$(TEST_INPUT): $(TEST_INPUT_SRC) $(LIBMR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $(TEST_INPUT_SRC) $(LIBMR) -pthread

$(TEST_MAPPER): $(TEST_MAPPER_SRC) $(LIBMR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $(TEST_MAPPER_SRC) $(LIBMR) -pthread

clean:
	rm -f $(LIBMR) $(LIBMR_OBJS) $(EXAMPLE) $(TEST_LOG) $(TEST_INPUT) $(TEST_MAPPER)
