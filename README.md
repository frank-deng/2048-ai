AI for the [2048 game](http://gabrielecirulli.github.io/2048/). This uses *expectimax optimization*, along with a highly-efficient bitboard representation to search upwards of 10 million moves per second on recent hardware. Heuristics used include bonuses for empty squares and bonuses for placing large values near edges and corners. Read more about the algorithm on the [StackOverflow answer](https://stackoverflow.com/a/22498940/1204143).

Building (Unix/Linux/OS X)
--------------------------

Execute

    ./configure
    make

in a terminal. Any relatively recent C++ compiler should be able to build the output.

Note that you don't do `make install`; this program is meant to be run from anywhere.

Running
-------

This program will run indefinitely until you send SIGINT signal to this process.
