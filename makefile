2048 : 2048.o
	g++ -O3 -pthread -std=gnu++11 -o $@ $^

2048.o : 2048.cpp
	g++ -O3 -pthread -std=gnu++11 -c -o $@ $<

.PHONY: clean
clean:
	rm 2048 2048.o

