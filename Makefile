CFLAGS=-g -Wall -pedantic

.PHONY: all
all: mysh

mysh: mysh.c
	gcc $(CFLAGS) -o $@ $^

.PHONY: clean
clean:
	rm -f mysh
