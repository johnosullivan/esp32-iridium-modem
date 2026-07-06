CC ?= gcc
CFLAGS ?= -Wall -Wextra -Werror -std=c11
ROOT := ../..
BUILD := build

SRCS := test_main.c test_framework.c test_sbd_parser.c test_uart_framing.c $(ROOT)/iridium_parser.c
OBJS := $(patsubst %.c,$(BUILD)/%.o,$(notdir $(SRCS)))
VPATH := . $(ROOT)

.PHONY: all test clean

all: test

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) $(CFLAGS) -I$(ROOT) -I. -c $< -o $@

$(BUILD)/iridium_host_tests: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

test: $(BUILD)/iridium_host_tests
	./$(BUILD)/iridium_host_tests

clean:
	rm -rf $(BUILD)
