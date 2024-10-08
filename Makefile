CFLAGS=-std=gnu17 -Werror -Wall -Wno-trigraphs -Os -pedantic
DEBUG_CFLAGS=-fsanitize=address -fsanitize=undefined -g -Og
COMMON:=core.o progress_bar.o message.o entry.o stream.o
LDLIBS=-lm
CC:=gcc
ALL_FILES :=$(wildcard *.[c|h])
MAKEFLAGS += --jobs=$(shell nproc)

.PHONY: default all clean format debug

default: debug

debug: CFLAGS+=$(DEBUG_CFLAGS)
debug: all

all: server client

clean:
	rm -f *.o client server

format: 
	clang-format -i $(ALL_FILES)

server: $(COMMON) server.o
	$(CC) $(CFLAGS) $(LDLIBS) -o server server.o $(COMMON)

client: $(COMMON) client.o
	$(CC) $(CFLAGS) $(LDLIBS) -o client client.o $(COMMON)

