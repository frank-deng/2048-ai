#ifndef __util_h__
#define __util_h__

#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <pthread.h>

#ifndef min
#define min(a,b) ((a) < (b) ? (a) : (b))
#endif

#ifndef max
#define max(a,b) ((a) > (b) ? (a) : (b))
#endif

enum{
    E_OK,
    E_INVAL,
    E_FILEIO,
    E_NOSPACE,
    E_TIMEOUT,
    E_AGAIN,
};

static pthread_mutex_t rand_mutex = PTHREAD_MUTEX_INITIALIZER;

static inline uint32_t unif_random(uint32_t n) {
    static bool seeded = false;
    pthread_mutex_lock(&rand_mutex);
    if(!seeded) {
        int fd = open("/dev/urandom", O_RDONLY);
        uint16_t seed[3];
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
    double rand_val=drand48();
    pthread_mutex_unlock(&rand_mutex);
    return (uint32_t)(rand_val * n);
}

#endif
