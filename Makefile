CC = gcc
CFLAGS = -Wall -Wextra -g -std=c11
LDFLAGS =

# Main program
TARGET = main
SRCS = main.c alloc.c heap.c
OBJS = $(SRCS:.c=.o)

TEST_TARGET = test_alloc
TEST_SRCS = test_alloc.c alloc.c heap.c
TEST_OBJS = $(TEST_SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(LDFLAGS)

$(TEST_TARGET): $(TEST_OBJS)
	$(CC) $(TEST_OBJS) -o $(TEST_TARGET) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

test: $(TEST_TARGET)
	./$(TEST_TARGET)

clean:
	rm -f $(OBJS) $(TEST_OBJS) $(TARGET) $(TEST_TARGET)

rebuild: clean all

.PHONY: all clean rebuild test