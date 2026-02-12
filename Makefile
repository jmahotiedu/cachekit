CC ?= gcc
CFLAGS = -Wall -Wextra -Wpedantic -std=c11 -O2 -g -D_POSIX_C_SOURCE=200809L
LDFLAGS = -lm
# Windows (MinGW): make LDFLAGS="-lm -lws2_32"

SRCDIR = src
TESTDIR = tests
BENCHDIR = bench
BUILDDIR = build

SRCS = $(wildcard $(SRCDIR)/*.c)
OBJS = $(patsubst $(SRCDIR)/%.c, $(BUILDDIR)/%.o, $(SRCS))

# exclude main.o for tests; exclude server.o so tests don't need Winsock on Windows
LIB_OBJS = $(filter-out $(BUILDDIR)/main.o, $(OBJS))
TEST_LIB_OBJS = $(filter-out $(BUILDDIR)/main.o $(BUILDDIR)/server.o, $(OBJS))

TEST_SRCS = $(wildcard $(TESTDIR)/*.c)
TEST_OBJS = $(patsubst $(TESTDIR)/%.c, $(BUILDDIR)/%.o, $(TEST_SRCS))

TARGET = cachekit
TEST_TARGET = $(BUILDDIR)/test_runner
BENCH_TARGET = benchmark

.PHONY: all clean test bench asan

all: $(TARGET)

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR)/%.o: $(TESTDIR)/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -I$(SRCDIR) -c $< -o $@

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(TEST_TARGET): $(TEST_LIB_OBJS) $(TEST_OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BENCH_TARGET): $(BUILDDIR)/benchmark.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BUILDDIR)/benchmark.o: $(BENCHDIR)/benchmark.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -I$(SRCDIR) -c $< -o $@

test: $(TEST_TARGET)
	./$(TEST_TARGET)

bench: $(BENCH_TARGET)
	./$(BENCH_TARGET)

asan: CFLAGS += -fsanitize=address -fno-omit-frame-pointer
asan: LDFLAGS += -fsanitize=address
asan: clean $(TEST_TARGET)
	./$(TEST_TARGET)

clean:
	rm -rf $(BUILDDIR) $(TARGET) $(BENCH_TARGET)
