#ifndef __worker_h__
#define __worker_h__

#include <stdio.h>
#include <pthread.h>
#include "2048.h"
#include "random.h"

#define MAX_CONNECTIONS (16)

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

typedef struct{
    const char *log_path;
    const char *snapshot_path;
    const char *socket_path;
    FILE *fp_log;
    FILE *fp_snapshot;
    int fd_socket;
    bool socket_created;
    int clients[MAX_CONNECTIONS];
}fileinfo_t;

struct worker_s {
    pthread_mutex_t log_mutex;
    volatile bool running;
    table_data_t *table_data;
    fileinfo_t fileinfo;
    uint16_t thread_count;
    thread_data_t thread_data[0];
};
typedef struct worker_s worker_t;

typedef struct{
    uint16_t thread_count;
    const char *log_path;
    const char *snapshot_path;
    const char *socket_path;
}worker_param_t;

#ifdef __cplusplus
extern "C" {
#endif

worker_t *worker_start(worker_param_t *param);
void worker_stop(worker_t *worker);
int worker_pipe_handler(worker_t *worker);

#ifdef __cplusplus
}
#endif

#endif
