ALL_FILES=$(wildcard *.[c|h])
CFLAGS=-std=gnu17 -Werror -Wall -Wno-trigraphs -Og -pedantic
COMMON=core.o progress_bar.o size_info.o message.o
LDLIBS=-lm

all: server client new_client

server: $(COMMON) server.o
	gcc $(CFLAGS) $(LDLIBS) -o server server.o $(COMMON)

client: $(COMMON) client.o
	gcc $(CFLAGS) $(LDLIBS) -o client client.o $(COMMON)

new_client: $(COMMON) new_client.o
	gcc $(CFLAGS) $(LDLIBS) -o new_client new_client.o $(COMMON)

clean:
	rm -f *.o client server

format: 
	clang-format -i $(ALL_FILES)

