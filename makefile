TARGET=2048ai
OBJS=2048.o table.o fileio.o worker.o viewer.o main.o
HEADERS=2048.h util.h random.h fileio.h worker.h viewer.h

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

fileio.o: fileio.c $(HEADERS)
	gcc $(CFLAGS) -c -o $@ $<

worker.o: worker.c $(HEADERS)
	gcc $(CFLAGS) -c -o $@ $<

viewer.o: viewer.c $(HEADERS)
	gcc $(CFLAGS) -c -o $@ $<

main.o: main.c $(HEADERS)
	gcc $(CFLAGS) -c -o $@ $<

.PHONY: clean
clean:
	-rm $(TARGET) *.o
