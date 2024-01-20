#ifndef __worker_h__
#define __worker_h__

#include <stdio.h>
#include <pthread.h>
#include "2048.h"
#include "random.h"

struct worker_s;
typedef struct {
    pthread_t tid;
    struct worker_s *worker;
    pthread_rwlock_t rwlock;
    rand_t rand;
    uint32_t moveno;
    uint32_t scoreoffset;
    board_t board;
} thread_data_t;

struct worker_s {
    pthread_mutex_t log_mutex;
    volatile bool running;
    table_data_t *table_data;
    FILE *fp_log;
    FILE *fp_snapshot;
    pthread_t tid_snapshot;
    uint16_t thread_count;
    thread_data_t thread_data[0];
};
typedef struct worker_s worker_t;

#ifdef __cplusplus
extern "C" {
#endif

worker_t *worker_init(uint16_t thread_count, const char *log_path, const char *snapshot_path);
void worker_start(worker_t *worker);
void worker_stop(worker_t *worker);
void worker_close(worker_t *worker);

#ifdef __cplusplus
}
#endif

#endif
