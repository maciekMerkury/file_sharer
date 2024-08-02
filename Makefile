CFLAGS=-std=gnu17 -Werror -Wall -Wno-trigraphs -Os -pedantic
DEBUG_CFLAGS=-fsanitize=address -fsanitize=undefined -g -Og
COMMON=core.o progress_bar.o size_info.o message.o entry.o
LDLIBS=-lm
CC=gcc
ALL_FILES=$(wildcard *.[c|h])

default: debug

debug: CFLAGS+=$(DEBUG_CFLAGS)
debug: all

.PHONY: default all clean format debug

all: server client

server: $(COMMON) server.o
	gcc $(CFLAGS) $(LDLIBS) -o server server.o $(COMMON)

client: $(COMMON) client.o
	gcc $(CFLAGS) $(LDLIBS) -o client client.o $(COMMON)

clean:
	rm -f *.o client server

format: 
	clang-format -i $(ALL_FILES)

