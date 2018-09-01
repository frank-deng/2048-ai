#!/usr/bin/env python3

import lib2048;

if '__main__' == __name__:
    lib2048.findBestMove([
        0, 0, 0, 2,
        2, 2, 4, 8,
        16, 32, 64, 128,
        256, 512, 1024, 2048,
    ]);

