#!/usr/bin/env python3

from ctypes import cdll
lib2048 = cdll.LoadLibrary('./lib2048.so');

def __trailingZeros(num):
    if (num == 0):
        return 0;
    n, result = num, 0;
    while not (n & 1):
        n >>= 1;
        result += 1;
    return result;

def findBestMove(board):
    boardHex = 0;
    for i in range(4):
        for j in range(4):
            n = __trailingZeros(board[(3-i)*4+(3-j)]);
            boardHex <<= 4;
            boardHex |= n;
    print('%x'%boardHex);
    moveTable = {
        '0': 'UP',
        '1': 'DOWN',
        '2': 'LEFT',
        '3': 'RIGHT',
    }
    return moveTable.get(str(lib2048.find_best_move(boardHex)));

if '__main__' == __name__:
    print(findBestMove([
        8, 2, 4, 2,
        2, 2, 4, 8,
        128, 64, 32, 16,
        256, 512, 1024, 2048,
    ]));
    print(findBestMove([
        0, 0, 0, 0,
        8, 4, 2, 2,
        16, 32, 64, 128,
        2048, 1024, 512, 256,
    ]));

