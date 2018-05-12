2048 : 2048.o nprocs.o
	g++ -O3 -pthread -o $@ $^

2048.o : 2048.cpp
	g++ -O3 -pthread -c -o $@ $<

nprocs.o: nprocs.c
	gcc -O3 -c -o $@ $<

.PHONY: clean
clean:
	-rm 2048 *.o

