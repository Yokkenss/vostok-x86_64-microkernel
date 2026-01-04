.SUFFIXES:

CC := cc
BUILD := build
TARGET := $(BUILD)/iskrac
SOURCES := \
	src/main.c \
	src/diagnostic.c \
	src/token.c \
	src/lexer.c \
	src/ast.c \
	src/parser.c \
	src/semantic.c \
	src/hir.c \
	src/backend.c
OBJECTS := $(SOURCES:src/%.c=$(BUILD)/%.o)
CFLAGS := -std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror
CPPFLAGS := -Isrc

.PHONY: all test clean

all: $(TARGET)

$(BUILD):
	mkdir -p $@

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) $(OBJECTS) -o $@

test: $(TARGET)
	sh tests/run.sh $(TARGET)

clean:
	rm -rf $(BUILD)
