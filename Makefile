CC      = gcc
CFLAGS  = -std=c11 -pthread -Wall -Wextra
CPPFLAGS = -Iinclude
AR      = ar
ARFLAGS = rcs

LIBMR      = libmr.a
LIBMR_OBJS = src/mr.o src/io.o src/log.o src/input.o src/queue.o src/mapper_proc.o 	src/reducer_proc.o

EXAMPLE_MINIMAL     = examples/minimal
EXAMPLE_MINIMAL_SRC = examples/minimal.c
EXAMPLE_WORD_COUNT     = examples/word-count
EXAMPLE_WORD_COUNT_SRC = examples/word-count.c

TEST_LOG      	= tests/log
TEST_LOG_SRC  	= tests/log.c
TEST_INPUT    	= tests/input
TEST_INPUT_SRC 	= tests/input.c
TEST_MAPPER   	= tests/mapper
TEST_MAPPER_SRC = tests/mapper.c
TEST_IO         = tests/io
TEST_IO_SRC     = tests/io.c
TEST_REDUCER    = tests/reducer
TEST_REDUCER_SRC = tests/reducer.c
TEST_INTEGRATION     = tests/integration
TEST_INTEGRATION_SRC = tests/integration.c

.PHONY: all test clean

all: $(LIBMR) $(EXAMPLE_MINIMAL) $(EXAMPLE_WORD_COUNT)

$(LIBMR): $(LIBMR_OBJS)
	$(AR) $(ARFLAGS) $@ $^

$(EXAMPLE_MINIMAL): $(EXAMPLE_MINIMAL_SRC) $(LIBMR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $(EXAMPLE_MINIMAL_SRC) $(LIBMR) -pthread

$(EXAMPLE_WORD_COUNT): $(EXAMPLE_WORD_COUNT_SRC) $(LIBMR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $(EXAMPLE_WORD_COUNT_SRC) $(LIBMR) -pthread

src/%.o: src/%.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

test: $(LIBMR) $(TEST_LOG) $(TEST_INPUT) $(TEST_MAPPER) $(TEST_IO) $(TEST_REDUCER) $(TEST_INTEGRATION)
	./$(TEST_LOG)
	./$(TEST_INPUT)
	./$(TEST_MAPPER)
	./$(TEST_IO)
	./$(TEST_REDUCER)
	./$(TEST_INTEGRATION)

$(TEST_LOG): $(TEST_LOG_SRC) $(LIBMR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $(TEST_LOG_SRC) $(LIBMR) -pthread

$(TEST_INPUT): $(TEST_INPUT_SRC) $(LIBMR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $(TEST_INPUT_SRC) $(LIBMR) -pthread

$(TEST_MAPPER): $(TEST_MAPPER_SRC) $(LIBMR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $(TEST_MAPPER_SRC) $(LIBMR) -pthread

$(TEST_IO): $(TEST_IO_SRC) $(LIBMR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $(TEST_IO_SRC) $(LIBMR) -pthread

$(TEST_REDUCER): $(TEST_REDUCER_SRC) $(LIBMR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $(TEST_REDUCER_SRC) $(LIBMR) -pthread

$(TEST_INTEGRATION): $(TEST_INTEGRATION_SRC) $(LIBMR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $(TEST_INTEGRATION_SRC) $(LIBMR) -pthread

clean:
	rm -f $(LIBMR) $(LIBMR_OBJS) $(EXAMPLE_MINIMAL) $(EXAMPLE_WORD_COUNT) $(TEST_LOG) $(TEST_INPUT) $(TEST_MAPPER) $(TEST_IO) $(TEST_REDUCER) $(TEST_INTEGRATION)
