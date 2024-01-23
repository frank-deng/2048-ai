#ifndef __fileio_h__
#define __fileio_h__

#include "worker.h"

#ifdef __cplusplus
extern "C" {
#endif

int init_files(FILE **fp_log_out, FILE **fp_snapshot_out,
    const char *log_path, const char *snapshot_path);
void close_files(FILE *fp_log, FILE *fp_snapshot);
int write_log(thread_data_t *thread_data);
int read_snapshot(worker_t *worker);
int write_snapshot(worker_t *worker);

int init_pipe(const char *pipe_path);
void close_pipe(const char *pipe_path);

#ifdef __cplusplus
}
#endif

#endif
