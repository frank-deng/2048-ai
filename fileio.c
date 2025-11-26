#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>
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
int wait_daemon(bool start_action, const char *socket_path, time_t timeout)
{
    int rc=E_TIMEOUT;
    time_t t0=time(NULL),t;
    do{
        t=time(NULL);
        struct stat stat_in;
        if(start_action){
            if(stat(socket_path,&stat_in)==0){
                rc=E_OK;
                break;
            }
        }else{
            if(stat(socket_path,&stat_in)!=0){
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
    size_t i;
    if(NULL==info || NULL==info->log_path || NULL==info->snapshot_path ||
        NULL==info->socket_path){
        fprintf(stderr,"Invalid param.\n");
        return E_INVAL;
    }
    info->fp_log=info->fp_snapshot=NULL;
    info->fd_socket=-1;
    info->socket_created=false;
    for(i=0; i<MAX_CONNECTIONS; i++){
        info->clients[i]=-1;
    }
    
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
    
    int fd=socket(AF_UNIX,SOCK_STREAM,0);
    if(fd<0){
        goto error_exit;
    }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    info->fd_socket=fd;
    struct sockaddr_un addr;
    addr.sun_family=AF_UNIX;
    strncpy(addr.sun_path, info->socket_path, sizeof(addr.sun_path)-1);
    if(bind(fd, (const struct sockaddr *)&addr, sizeof(addr)) != 0){
        fprintf(stderr,"Failed to create socket %s: %s.\n",info->socket_path,strerror(errno));
        goto error_exit;
    }
    info->socket_created=true;
    if(listen(fd,1)<0){  
        fprintf(stderr, "Listen failed: %s\n", strerror(errno));  
        goto error_exit;
    }
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
    size_t i;
    for(i=0; i<MAX_CONNECTIONS; i++){
        int fd=info->clients[i];
        if(fd>=0){
            close(fd);
        }
    }
    if(info->fd_socket >= 0){
        close(info->fd_socket);
    }
    if(info->socket_created){
        unlink(info->socket_path);
    }
}
int write_log(thread_data_t *thread_data)
{
    pthread_rwlock_rdlock(&thread_data->rwlock);
    board_t board=thread_data->board;
    uint32_t score_offset=thread_data->scoreoffset;
    uint32_t moveno=thread_data->moveno;
    pthread_rwlock_unlock(&thread_data->rwlock);

    if (moveno==0 || board==0){
	return E_INVAL;
    }
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
static int add_fd(size_t count, int *fd_arr, int fd)
{
    size_t i;
    int res=-1;
    for(i=0; i<count; i++){
        int val=fd_arr[i];
        if(val==fd){
            return i;
        }else if(val<0 && res<0){
            res=i;
        }
    }
    if(res>=0){
        fd_arr[res]=fd;
    }
    return res;
}
static void del_fd(size_t count, int *fd_arr, int fd)
{
    size_t i;
    for(i=0; i<count; i++){
        if(fd_arr[i]==fd){
            fd_arr[i]=-1;
        }
    }
}
static void output_board_all(int fd,worker_t *worker)
{
    char buf[128];
    snprintf(buf,sizeof(buf),"%u\n",worker->thread_count);
    int rc=write(fd,buf,strlen(buf));
    if(rc<=0){
        fprintf(stderr,"Failed to write pipe %d %d\n",rc,errno);
    }
    uint16_t i;
    for (i = 0; i < worker->thread_count; i++) {
        thread_data_t *thread_data=&(worker->thread_data[i]);
        pthread_rwlock_rdlock(&(thread_data->rwlock));
        board_t board=thread_data->board;
        uint32_t score_offset=thread_data->scoreoffset;
        uint32_t moveno=thread_data->moveno;
        pthread_rwlock_unlock(&(thread_data->rwlock));
        uint32_t score=score_board(worker->table_data,board)-score_offset;
        snprintf(buf,sizeof(buf),"%u,%u,%u,%016llx\n",i,moveno,score,board);
        int rc=write(fd,buf,strlen(buf));
        if(rc<=0){
            fprintf(stderr,"Failed to write pipe %d %d\n",rc,errno);
        }
    }
}
static int session_handler(worker_t *worker,int fd)
{
    char cmd='\0';
    int rc=read(fd,&cmd,sizeof(cmd));
    if(rc<0 && errno==EAGAIN){
        return E_AGAIN;
    }else if(rc<=0){
        return E_FILEIO;
    }
    switch(cmd){
        case 'Q':
        case 'q':
            worker->running=false;
        break;
        case 'B':
        case 'b':
            output_board_all(fd,worker);
        break;
    }
    return E_OK;
}
void socket_handler(worker_t *worker)
{
    size_t i;
    fd_set readset;
    int listenfd=worker->fileinfo.fd_socket;
    int *fd_arr=worker->fileinfo.clients;
    FD_ZERO(&readset);
    FD_SET(listenfd,&readset);
    int maxfd=listenfd;
    for(i=0; i<MAX_CONNECTIONS; i++){
        int fd=fd_arr[i];
        if(fd<0){
            continue;
        }
        FD_SET(fd,&readset);
        if(fd>maxfd){
            maxfd=fd;
        }
    }
    struct timeval tm;
    tm.tv_sec=0;
    tm.tv_usec=10000;
    if(select(maxfd+1,&readset,NULL,NULL,&tm)<=0){
        return;
    }
    if(FD_ISSET(listenfd,&readset)){
        struct sockaddr_in clientaddr;
        socklen_t clientaddrlen=sizeof(clientaddr);
        int clientfd = accept(listenfd,(struct sockaddr *)&clientaddr,&clientaddrlen);
        if(clientfd >= 0){
            int idx=add_fd(MAX_CONNECTIONS,fd_arr,clientfd);
            if(idx<0){
                close(clientfd);
            }
        }
        return;
    }
    for(i=0; i<MAX_CONNECTIONS; i++){
        int fd=fd_arr[i];
        if(fd<0 || !FD_ISSET(fd,&readset)){
            continue;
        }
        if(E_FILEIO==session_handler(worker,fd)){
            close(fd);
            del_fd(MAX_CONNECTIONS,fd_arr,fd);
        }
    }
}
