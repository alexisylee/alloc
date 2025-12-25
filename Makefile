CC = gcc

# Base flags
BASE_CFLAGS = -pthread -Wall -Wextra -std=c11
BASE_LDFLAGS = -pthread

# Debug build flags (with ThreadSanitizer for concurrency bugs)
DEBUG_CFLAGS = $(BASE_CFLAGS) -g -O0 -fsanitize=thread -fno-omit-frame-pointer
DEBUG_LDFLAGS = $(BASE_LDFLAGS) -fsanitize=thread

# Release build flags (optimized)
RELEASE_CFLAGS = $(BASE_CFLAGS) -O3 -DNDEBUG -march=native
RELEASE_LDFLAGS = $(BASE_LDFLAGS)

# Default to debug build
CFLAGS = $(DEBUG_CFLAGS)
LDFLAGS = $(DEBUG_LDFLAGS)

# Source files
MAIN_SRCS = main.c alloc.c heap.c
TEST_SRCS = test_alloc.c alloc.c heap.c
BENCH_SRCS = benchmark.c alloc.c heap.c

# Targets
MAIN_TARGET = main
TEST_TARGET = test_alloc
BENCH_TARGET = bench

# Object files
MAIN_OBJS = $(MAIN_SRCS:.c=.o)
TEST_OBJS = $(TEST_SRCS:.c=.o)
BENCH_OBJS = $(BENCH_SRCS:.c=.o)

# Default target
all: debug

# Debug build (default)
debug: CFLAGS = $(DEBUG_CFLAGS)
debug: LDFLAGS = $(DEBUG_LDFLAGS)
debug: clean_objs $(TEST_TARGET)

release: CFLAGS = $(RELEASE_CFLAGS)
release: LDFLAGS = $(RELEASE_LDFLAGS)
release: clean_objs $(TEST_TARGET)

# Main program
$(MAIN_TARGET): $(MAIN_OBJS)
	$(CC) $(MAIN_OBJS) -o $(MAIN_TARGET) $(LDFLAGS)

# Test program
$(TEST_TARGET): $(TEST_OBJS)
	$(CC) $(TEST_OBJS) -o $(TEST_TARGET) $(LDFLAGS)

# Benchmark program
$(BENCH_TARGET): $(BENCH_OBJS)
	$(CC) $(BENCH_OBJS) -o $(BENCH_TARGET) $(LDFLAGS)

# Compile object files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Run unit tests
test_unit: $(TEST_TARGET)
	./$(TEST_TARGET) unit

# Run stress tests
test_stress: $(TEST_TARGET)
	./$(TEST_TARGET) stress

# Run concurrent tests
test_concurrent: $(TEST_TARGET)
	./$(TEST_TARGET) concurrent

# Run all tests
test_all: $(TEST_TARGET)
	./$(TEST_TARGET) all

# Default test target (runs all tests)
test: test_all

# Build benchmark with release flags
build_benchmark: CFLAGS = $(RELEASE_CFLAGS)
build_benchmark: LDFLAGS = $(RELEASE_LDFLAGS)
build_benchmark: clean_objs $(BENCH_TARGET)

# Run benchmark (builds first if needed)
benchmark: build_benchmark
	./$(BENCH_TARGET)

# Clean only object files
clean_objs:
	rm -f $(MAIN_OBJS) $(TEST_OBJS) $(BENCH_OBJS)

# Clean everything
clean:
	rm -f $(MAIN_OBJS) $(TEST_OBJS) $(BENCH_OBJS) $(MAIN_TARGET) $(TEST_TARGET) $(BENCH_TARGET)
	rm -rf *.dSYM

rebuild: clean all


.PHONY: all debug release test test_unit test_stress test_concurrent test_all build_benchmark benchmark clean clean_objs rebuild help