CC ?= gcc
CFLAGS ?= -Wall -Wextra -Werror -std=c11
ROOT := ../..
BUILD := build

TEST_SRCS := test_main.c test_framework.c test_sbd_parser.c test_uart_framing.c \
             test_serial_replay.c $(ROOT)/iridium_parser.c $(ROOT)/iridium_sim.c
REPLAY_SRCS := replay_main.c $(ROOT)/iridium_parser.c $(ROOT)/iridium_sim.c

TEST_OBJS := $(patsubst %.c,$(BUILD)/%.o,$(notdir $(TEST_SRCS)))
REPLAY_OBJS := $(patsubst %.c,$(BUILD)/%.o,$(notdir $(REPLAY_SRCS)))
VPATH := . $(ROOT)

.PHONY: all test replay clean

all: test

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) $(CFLAGS) -I$(ROOT) -I. -c $< -o $@

$(BUILD)/iridium_host_tests: $(TEST_OBJS)
	$(CC) $(CFLAGS) -o $@ $(TEST_OBJS)

$(BUILD)/iridium_sim_replay: $(REPLAY_OBJS)
	$(CC) $(CFLAGS) -o $@ $(REPLAY_OBJS)

test: $(BUILD)/iridium_host_tests
	./$(BUILD)/iridium_host_tests

replay: $(BUILD)/iridium_sim_replay
	./$(BUILD)/iridium_sim_replay fixtures/mo_send_success.txt

clean:
	rm -rf $(BUILD)
