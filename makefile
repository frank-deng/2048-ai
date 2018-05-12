2048 : 2048.o nprocs.o
	g++ -O3 -pthread -std=c++11 -o $@ $^

2048.o : 2048.cpp
	g++ -O3 -pthread -std=c++11 -c -o $@ $<

nprocs.o: nprocs.c
	gcc -O3 -pthread -c -o $@ $<

.PHONY: clean
clean:
	-rm 2048 *.o

