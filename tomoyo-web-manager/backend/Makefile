CC      ?= gcc
CFLAGS  ?= -std=c11 -Wall -Wextra -O2 -D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L
LDFLAGS ?= -lpthread

SRC_DIR := src
BUILD_DIR := build
BIN := tomoyo-web-manager

SRCS := $(wildcard $(SRC_DIR)/*.c)
OBJS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))

.PHONY: all clean run

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

run: all
	./$(BIN) config/tomoyo-web-manager.conf

clean:
	rm -rf $(BUILD_DIR) $(BIN)
