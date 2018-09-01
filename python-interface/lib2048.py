#!/usr/bin/env python3

from ctypes import cdll
lib2048 = cdll.LoadLibrary('./lib2048.so');

def __trailingZeros(num):
    result = 0;
    n = num;
    while not (n & 1):
        n >>= 1;
        result += 1;
    return result;

def findBestMove(board):
    boardHex = 0;
    for num in board:
        n = __trailingZeros(num);
        boardHex |= n;
        boardHex <<= 4;
    print(boardHex);
    moveTable = {
        '0': 'UP',
        '1': 'DOWN',
        '2': 'LEFT',
        '3': 'RIGHT',
    }
    return moveTable.get(lib2048.find_best_move(boardHex));

