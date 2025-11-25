# Use `make old_android=true` to compile on old android devices
TARGET=2048ai
OBJS=2048.o table.o fileio.o worker.o viewer.o main.o
HEADERS=2048.h util.h random.h fileio.h worker.h viewer.h

ifdef old_android
CC=arm-linux-androideabi-gcc
CPP=arm-linux-androideabi-g++
CFLAGS=-O3
else
CC=clang
CPP=clang++
CFLAGS=-O3
endif

CPPFLAGS=-std=c++11
LDFLAGS=-O3 -std=c++11

$(TARGET): $(OBJS)
	$(CPP) $(LDFLAGS) -o $@ $^

2048.o : 2048.cpp $(HEADERS)
	$(CPP) $(CFLAGS) $(CPPFLAGS)  -c -o $@ $<

table.o: table.c $(HEADERS)
	$(CC) $(CFLAGS) -c -o $@ $<

fileio.o: fileio.c $(HEADERS)
	$(CC) $(CFLAGS) -c -o $@ $<

worker.o: worker.c $(HEADERS)
	$(CC) $(CFLAGS) -c -o $@ $<

viewer.o: viewer.c $(HEADERS)
	$(CC) $(CFLAGS) -c -o $@ $<

main.o: main.c $(HEADERS)
	$(CC) $(CFLAGS) -c -o $@ $<

.PHONY: clean
clean:
	-rm $(TARGET) *.o

