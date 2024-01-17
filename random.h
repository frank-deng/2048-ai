#ifndef random_h
#define random_h

#include <stdint.h>
#include <stdbool.h>

#define RAND_N 624
#define RANDOM_MAX (0xffffffff)

typedef struct {
    uint32_t mt[RAND_N];
    uint16_t index;
} rand_t;

enum {
    N = RAND_N,
    M = 397,
    R = 31,
    A = 0x9908B0DF,
    F = 1812433253,
    U = 11,
    S = 7,
    B = 0x9D2C5680,
    T = 15,
    C = 0xEFC60000,
    L = 18,
    MASK_LOWER = (1ull << R) - 1,
    MASK_UPPER = (1ull << R)
};

static inline void initRandom(rand_t* rand, uint32_t seed)
{
    uint16_t i;
    rand->mt[0] = seed;
    for (i = 1; i < N; i++) {
        rand->mt[i] = (F * (rand->mt[i - 1] ^ (rand->mt[i - 1] >> 30)) + i);
    }
    rand->index = N;
};
static inline void twist(rand_t* rand)
{
    uint32_t  i, x, xA;
    for (i = 0; i < N; i++) {
        x = (rand->mt[i] & MASK_UPPER) + (rand->mt[(i + 1) % N] & MASK_LOWER);
        xA = x >> 1;
        if (x & 0x1) {
            xA ^= A;
        }
        rand->mt[i] = rand->mt[(i + M) % N] ^ xA;
    }
    rand->index = 0;
}
static inline uint32_t getRandom(rand_t* rand)
{
    uint32_t y;
    int i = rand->index;
    if (rand->index >= N) {
        twist(rand);
        i = rand->index;
    }
    y = rand->mt[i];
    rand->index = i + 1;
    y ^= (y >> U);
    y ^= (y << S) & B;
    y ^= (y << T) & C;
    y ^= (y >> L);
    return y;
}

#endif
