#ifndef __util_h__
#define __util_h__

#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>

#ifndef min
#define min(a,b) ((a) < (b) ? (a) : (b))
#endif

#ifndef max
#define max(a,b) ((a) > (b) ? (a) : (b))
#endif

static inline uint32_t unif_random(unsigned n) {
    static bool seeded = false;
    if(!seeded) {
        int fd = open("/dev/urandom", O_RDONLY);
        unsigned short seed[3];
        if(fd < 0 || read(fd, seed, sizeof(seed)) < (int)sizeof(seed)) {
            srand48(time(NULL));
        } else {
            seed48(seed);
        }
        if(fd >= 0) {
            close(fd);
		}
        seeded = true;
    }
    return (uint32_t)(drand48() * n);
}

#endif
