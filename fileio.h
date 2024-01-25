#ifndef __fileio_h__
#define __fileio_h__

#include "worker.h"

#ifdef __cplusplus
extern "C" {
#endif

int init_files(fileinfo_t *info);
void close_files(fileinfo_t *info);
int write_log(thread_data_t *thread_data);
int read_snapshot(worker_t *worker);
int write_snapshot(worker_t *worker);

#ifdef __cplusplus
}
#endif

#endif
