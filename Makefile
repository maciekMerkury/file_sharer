CC:=gcc
CFLAGS=-std=gnu17 -Werror -Wall -Wno-trigraphs -Os $(shell pkg-config --cflags libnotify)
LDLIBS= $(shell pkg-config --libs libnotify)
DEBUG_CFLAGS=-fsanitize=address -fsanitize=undefined -g -Og -pedantic
MAKEFLAGS += --jobs=$(shell nproc)

SRC_DIR := src
COMMON_DIR := $(SRC_DIR)/common
OBJ_DIR := obj

SRC := $(wildcard $(SRC_DIR)/**/*.[c|h])
COMMON_HDRS := $(wildcard $(COMMON_DIR)/*.h)
COMMON_SRC := $(wildcard $(COMMON_DIR)/*.c)
COMMON_OBJ := $(COMMON_SRC:$(COMMON_DIR)/%.c=$(OBJ_DIR)/%.o)

.PHONY: default all clean format debug

default: debug

debug: CFLAGS+=$(DEBUG_CFLAGS)
debug: all

all: server client

clean:
	rm -r $(OBJ_DIR) client server

format: 
	clang-format -i $(SRC)

$(OBJ_DIR):
	mkdir -p $@

$(OBJ_DIR)/%.o: $(COMMON_DIR)/%.c $(COMMON_HDRS) | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -I$(COMMON_DIR) -c $< -o $@

server: $(COMMON_OBJ) $(OBJ_DIR)/server.o
	$(CC) $(CFLAGS) $(LDLIBS) -o server $^

client: $(COMMON_OBJ) $(OBJ_DIR)/client.o
	$(CC) $(CFLAGS) $(LDLIBS) -o client $^

