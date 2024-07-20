SRC=$(wildcard *.c)
HDR=$(wildcard *.h)
CFLAGS=-std=gnu17 -Werror -Wall -Og

main: $(SRC)
	gcc -o $@ $^ $(CFLAGS) $(LIBS)

run: main
	./main

clean:
	rm -f *.o main

format: 
	clang-format -i $(SRC) $(HDR)


