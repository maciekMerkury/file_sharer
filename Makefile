ALL_FILES=$(wildcard *.[c|h])
CFLAGS=-std=gnu17 -Werror -Wall -Og
COMMON=core.o
LDLIBS=-lm

all: server client

server: $(COMMON) server.o
	gcc $(CFLAGS) $(LDLIBS) -o server server.o $(COMMON)

client: $(COMMON) client.o
	gcc $(CFLAGS) $(LDLIBS) -o client client.o $(COMMON)

clean:
	rm -f *.o client server

format: 
	clang-format -i $(ALL_FILES)

