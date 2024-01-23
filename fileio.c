#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include "fileio.h"

int init_files(FILE **fp_log_out, FILE **fp_snapshot_out,
    const char *log_path, const char *snapshot_path)
{
    if(NULL==fp_log_out || NULL==fp_snapshot_out){
        fprintf(stderr,"Invalid param.\n");
        return E_INVAL;
    }
    FILE *fp_log=NULL, *fp_snapshot=NULL;
    fp_log=fopen(log_path, "a");
    if(NULL==fp_log){
        fprintf(stderr,"Failed to open log file %s\n",log_path);
        goto error_exit;
    }
    if(flock(fileno(fp_log),LOCK_EX|LOCK_NB)!=0){
        fprintf(stderr,"Failed to lock log file %s, possibly other instance is running.\n",log_path);
        goto error_exit;
    }
    fp_snapshot=fopen(snapshot_path, "rb+");
    if(NULL==fp_snapshot){
        fp_snapshot=fopen(snapshot_path, "wb+");
    }
    if(NULL==fp_snapshot){
        fprintf(stderr,"Failed to open snapshot file %s\n",snapshot_path);
        goto error_exit;
    }
    if(flock(fileno(fp_snapshot),LOCK_EX|LOCK_NB)!=0){
        fprintf(stderr,"Failed to lock snapshot file %s, possibly other instance is running.\n",snapshot_path);
        goto error_exit;
    }
    *fp_log_out=fp_log;
    *fp_snapshot_out=fp_snapshot;
    return E_OK;
error_exit:
    if (NULL != fp_log) {
        flock(fileno(fp_log),LOCK_UN|LOCK_NB);
        fclose(fp_log);
    }
    if (NULL != fp_snapshot) {
        flock(fileno(fp_snapshot),LOCK_UN|LOCK_NB);
        fclose(fp_snapshot);
    }
    close_files(fp_log,fp_snapshot);
    *fp_log_out=NULL;
    *fp_snapshot_out=NULL;
    return E_FILEIO;
}
void close_files(FILE *fp_log, FILE *fp_snapshot)
{
    if (NULL != fp_log) {
        flock(fileno(fp_log),LOCK_UN|LOCK_NB);
        fclose(fp_log);
    }
    if (NULL != fp_snapshot) {
        flock(fileno(fp_snapshot),LOCK_UN|LOCK_NB);
        fclose(fp_snapshot);
    }
}
int write_log(thread_data_t *thread_data)
{
    pthread_rwlock_rdlock(&thread_data->rwlock);
    board_t board=thread_data->board;
    uint32_t score_offset=thread_data->scoreoffset;
    uint32_t moveno=thread_data->moveno;
    pthread_rwlock_unlock(&thread_data->rwlock);

    worker_t *worker=thread_data->worker;
    uint32_t score=score_board(worker->table_data,board)-score_offset;
    uint16_t max_rank=(1<<get_max_rank(board));

    pthread_mutex_lock(&worker->log_mutex);
    fprintf(worker->fp_log,"%u,%u,%u,%016lx\n",moveno,score,max_rank,board);
    fflush(worker->fp_log);
    pthread_mutex_unlock(&worker->log_mutex);
    return E_OK;
}
int read_snapshot(worker_t *worker)
{
    int i;
    for (i = 0; i < worker->thread_count; i++) {
        thread_data_t *thread_data=&(worker->thread_data[i]);
        board_t board;
        uint32_t score_offset;
        uint32_t moveno;
        fscanf(worker->fp_snapshot,"%u,%u,%016lx",&moveno,&score_offset,&board);
        pthread_rwlock_wrlock(&thread_data->rwlock);
        thread_data->moveno=moveno;
        thread_data->scoreoffset=score_offset;
        thread_data->board=board;
        pthread_rwlock_unlock(&thread_data->rwlock);
    }
    return E_OK;
}
int write_snapshot(worker_t *worker)
{
    fseek(worker->fp_snapshot,0,SEEK_SET);
    ftruncate(fileno(worker->fp_snapshot), 0);
    fseek(worker->fp_snapshot,0,SEEK_SET);
    int i;
    for (i = 0; i < worker->thread_count; i++) {
        thread_data_t *thread_data=&(worker->thread_data[i]);
        pthread_rwlock_rdlock(&(thread_data->rwlock));
        board_t board=thread_data->board;
        uint32_t score_offset=thread_data->scoreoffset;
        uint32_t moveno=thread_data->moveno;
        pthread_rwlock_unlock(&(thread_data->rwlock));
        fprintf(worker->fp_snapshot,"%u,%u,%016lx\n",moveno,score_offset,board);
    }
    fflush(worker->fp_snapshot);
    return E_OK;
}

int init_pipe(const char *pipe_path)
{
    int res=mkfifo(pipe_path, 0666);
    if(res < 0) {
        fprintf(stderr,"Failed to create fifo %s\n",pipe_path);
        return res;
    }
    return res;
}
void close_pipe(const char *pipe_path)
{
    if(pipe_path!=NULL){
        unlink(pipe_path);
    }
}
