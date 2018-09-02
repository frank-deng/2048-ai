#!/usr/bin/env python3

import ctypes;
lib2048 = ctypes.CDLL('./lib2048.so');
lib2048.find_best_move.argtypes = [ctypes.c_uint64];

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
    i = 0;
    for row in range(4):
        for col in range(4):
            n = __trailingZeros(board[row*4+col]);
            boardHex |= (int(n) << (i*4));
            i += 1;
    return {
        '0': 'UP',
        '1': 'DOWN',
        '2': 'LEFT',
        '3': 'RIGHT',
    }.get(str(lib2048.find_best_move(boardHex)));

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

