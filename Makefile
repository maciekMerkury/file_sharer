ALL_FILES=$(wildcard *.[c|h])
CFLAGS=-std=gnu17 -Werror -Wall -Og
COMMON=core.o

all: server client

server: $(COMMON) server.o
	gcc $(CFLAGS) -o server server.o $(COMMON)

client: $(COMMON) client.o
	gcc $(CFLAGS) -o client client.o $(COMMON)

clean:
	rm -f *.o client server

format: 
	clang-format -i $(ALL_FILES)

