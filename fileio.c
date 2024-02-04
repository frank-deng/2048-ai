#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include "fileio.h"

bool test_running(const char *log_path,const char *snapshot_path)
{
    bool running=false;
    int fd_log=open(log_path,O_RDWR|O_NONBLOCK);
    int fd_snapshot=open(snapshot_path,O_RDWR|O_NONBLOCK);
    if(fd_log>=0){
        if(flock(fd_log,LOCK_EX|LOCK_NB)!=0){
            running=true;
        }else{
            flock(fd_log,LOCK_UN|LOCK_NB);
        }
    }
    if(fd_snapshot>=0){
        if(flock(fd_snapshot,LOCK_EX|LOCK_NB)!=0){
            running=true;
        }else{
            flock(fd_snapshot,LOCK_UN|LOCK_NB);
        }
    }
    return running;
}
int wait_daemon(bool start_action,const char *pipe_in,const char *pipe_out,
    time_t timeout)
{
    int rc=E_TIMEOUT;
    time_t t0=time(NULL),t;
    do{
        t=time(NULL);
        struct stat stat_in,stat_out;
        if(start_action){
            if(stat(pipe_in,&stat_in)==0 && stat(pipe_out,&stat_out)==0){
                rc=E_OK;
                break;
            }
        }else{
            if(stat(pipe_in,&stat_in)!=0 && stat(pipe_out,&stat_out)!=0){
                rc=E_OK;
                break;
            }
        }
        usleep(1000);
    }while(t-t0 < timeout);
    return rc;
}
int init_files(fileinfo_t *info)
{
    if(NULL==info || NULL==info->log_path || NULL==info->snapshot_path ||
        NULL==info->pipe_in || NULL==info->pipe_out){
        fprintf(stderr,"Invalid param.\n");
        return E_INVAL;
    }
    info->fp_log=info->fp_snapshot=NULL;
    info->fd_in=info->fd_out=-1;
    info->pipe_in_created=info->pipe_out_created=false;
    
    FILE *fp_log=fopen(info->log_path, "a");
    if(NULL==fp_log){
        fprintf(stderr,"Failed to open log file %s\n",info->log_path);
        goto error_exit;
    }
    info->fp_log=fp_log;
    if(flock(fileno(fp_log),LOCK_EX|LOCK_NB)!=0){
        fprintf(stderr,"Failed to lock log file %s, possibly other instance is running.\n",info->log_path);
        goto error_exit;
    }
    
    FILE *fp_snapshot=fopen(info->snapshot_path, "rb+");
    if(NULL==fp_snapshot){
        fp_snapshot=fopen(info->snapshot_path, "wb+");
    }
    if(NULL==fp_snapshot){
        fprintf(stderr,"Failed to open snapshot file %s\n",info->snapshot_path);
        goto error_exit;
    }
    info->fp_snapshot=fp_snapshot;
    if(flock(fileno(fp_snapshot),LOCK_EX|LOCK_NB)!=0){
        fprintf(stderr,"Failed to lock snapshot file %s, possibly other instance is running.\n",info->snapshot_path);
        goto error_exit;
    }
    
    struct stat stat_pipe;
    if(stat(info->pipe_in,&stat_pipe)!=0){
        int res=mkfifo(info->pipe_in, 0666);
        if(res < 0) {
            fprintf(stderr,"Failed to create fifo %s\n",info->pipe_in);
            goto error_exit;
        }
    }
    info->pipe_in_created=true;
    
    if(stat(info->pipe_out,&stat_pipe)!=0){
        int res=mkfifo(info->pipe_out, 0666);
        if(res < 0) {
            fprintf(stderr,"Failed to create fifo %s\n",info->pipe_out);
            goto error_exit;
        }
    }
    info->pipe_out_created=true;
    
    int fd_in=open(info->pipe_in,O_RDWR|O_NONBLOCK);
    if(fd_in<0){
        fprintf(stderr,"Failed to open fifo %s\n",info->pipe_in);
        goto error_exit;
    }
    info->fd_in=fd_in;
    
    int fd_out=open(info->pipe_out,O_RDWR|O_NONBLOCK);
    if(fd_out<0){
        fprintf(stderr,"Failed to open fifo %s\n",info->pipe_out);
        goto error_exit;
    }
    info->fd_out=fd_out;
    return E_OK;
error_exit:
    close_files(info);
    return E_FILEIO;
}
void close_files(fileinfo_t *info)
{
    if (NULL != info->fp_log) {
        flock(fileno(info->fp_log),LOCK_UN|LOCK_NB);
        fclose(info->fp_log);
    }
    if (NULL != info->fp_snapshot) {
        flock(fileno(info->fp_snapshot),LOCK_UN|LOCK_NB);
        fclose(info->fp_snapshot);
    }
    if(info->fd_in >= 0){
        close(info->fd_in);
    }
    if(info->fd_out >= 0){
        close(info->fd_out);
    }
    if(info->pipe_in_created){
        unlink(info->pipe_in);
    }
    if(info->pipe_out_created){
        unlink(info->pipe_out);
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
    fprintf(worker->fileinfo.fp_log,"%u,%u,%u,%016llx\n",moveno,score,max_rank,board);
    fflush(worker->fileinfo.fp_log);
    pthread_mutex_unlock(&worker->log_mutex);
    return E_OK;
}
int read_snapshot(worker_t *worker)
{
    int i;
    for (i = 0; i < worker->thread_count; i++) {
        thread_data_t *thread_data=&(worker->thread_data[i]);
        board_t board=0;
        uint32_t score_offset=0;
        uint32_t moveno=0;
        fscanf(worker->fileinfo.fp_snapshot,"%u,%u,%llx",&moveno,&score_offset,&board);
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
    FILE *fp=worker->fileinfo.fp_snapshot;
    fseek(fp,0,SEEK_SET);
    ftruncate(fileno(fp), 0);
    fseek(fp,0,SEEK_SET);
    int i;
    for (i = 0; i < worker->thread_count; i++) {
        thread_data_t *thread_data=&(worker->thread_data[i]);
        pthread_rwlock_rdlock(&(thread_data->rwlock));
        board_t board=thread_data->board;
        uint32_t score_offset=thread_data->scoreoffset;
        uint32_t moveno=thread_data->moveno;
        pthread_rwlock_unlock(&(thread_data->rwlock));
        fprintf(fp,"%u,%u,%016llx\n",moveno,score_offset,board);
    }
    fflush(fp);
    return E_OK;
}
