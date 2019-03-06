CFLAGS = -Wall -pedantic -g -lpthread -lrt
pc: pc.o
	gcc -o pc pc.o $(CFLAGS)
pc.o: pc.c
	gcc -c pc.c $(CFLAGS)
clean:
	rm pc pc.o
