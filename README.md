AI for the [2048 game](http://gabrielecirulli.github.io/2048/). This uses *expectimax optimization*, along with a highly-efficient bitboard representation to search upwards of 10 million moves per second on recent hardware. Heuristics used include bonuses for empty squares and bonuses for placing large values near edges and corners. Read more about the algorithm on the [StackOverflow answer](https://stackoverflow.com/a/22498940/1204143).

Building and Running (Linux)
----------------------------

Use the commands below to build and run AI for the 2048 game.

	make
	./2048 log_file

This program will run indefinitely until you send SIGINT signal to this process (Probably using Ctrl-C).

