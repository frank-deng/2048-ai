TARGET=2048
OBJS=2048.o table.o main.o
HEADERS=2048.h util.h

CC=gcc
CPP=g++
CFLAGS=-O3 -pthread
CPPFLAGS=-std=c++11
LDFLAGS=-O3 -pthread -std=c++11

$(TARGET): $(OBJS)
	g++ $(LDFLAGS) -o $@ $^

2048.o : 2048.cpp $(HEADERS)
	g++ $(CFLAGS) $(CPPFLAGS)  -c -o $@ $<

table.o: table.c $(HEADERS)
	gcc $(CFLAGS) -c -o $@ $<

main.o: main.c $(HEADERS)
	gcc $(CFLAGS) -c -o $@ $<

.PHONY: clean
clean:
	-rm 2048 *.o
