SRC=$(wildcard *.c)
CFLAGS=-std=c17 -Werror -Wall -Og

main: $(SRC)
	gcc -o $@ $^ $(CFLAGS) $(LIBS)

run: main
	./main

clean:
	rm -f *.o main

format: 
	clang-format -i $(SOURCES) $(HEADERS)


