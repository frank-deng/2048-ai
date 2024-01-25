#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <signal.h>
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>
#include "util.h"
#include "viewer.h"

typedef uint64_t board_t;
typedef struct{
    int fd_in;
    int fd_out;
    volatile bool running;
    uint16_t cols;
    uint16_t thread_count;
    pthread_t tid;
}viewer_t;

static inline void _clrscr()
{
    printf("\033[2J");
}
static inline void goto_rowcol(uint8_t row, uint8_t col)
{
    printf("\033[%u;%uH",row+1,col+1);
}

static inline int get_thread_count(viewer_t *viewer, uint16_t *out)
{
    char cmd='t',buf[10];
    uint8_t retry=8;
    int rc=0;
    while(read(viewer->fd_out,buf,sizeof(buf))>0);
    rc=write(viewer->fd_in,&cmd,sizeof(cmd));
    if(rc<=0){
        return E_FILEIO;
    }
    do{
        usleep(10);
        rc=read(viewer->fd_out,buf,sizeof(buf)-1);
    }while(viewer->running && rc<0 && errno==EAGAIN && retry--);
    if(rc<=0){
        return E_FILEIO;
    }
    buf[rc]='\0';
    uint16_t thread_count=strtoul(buf,NULL,0);
    *out=thread_count;
    return E_OK;
}
void *viewer_thread(void *data);
int init_viewer(viewer_t *viewer, const char *pipe_in, const char *pipe_out)
{
    viewer->fd_in=open(pipe_in,O_RDWR|O_NONBLOCK);
    if(viewer->fd_in<0){
        fprintf(stderr,"Failed to open pipe %s\n",pipe_in);
        return E_FILEIO;
    }
    viewer->fd_out=open(pipe_out,O_RDWR|O_NONBLOCK);
    if(viewer->fd_out<0){
        close(viewer->fd_in);
        fprintf(stderr,"Failed to open pipe %s\n",pipe_in);
        return E_FILEIO;
    }
    viewer->running=true;
    viewer->cols=80;
    int res=get_thread_count(viewer,&viewer->thread_count);
    if(res!=E_OK){
        close(viewer->fd_in);
        close(viewer->fd_out);
        return res;
    }
    pthread_create(&viewer->tid,NULL,viewer_thread,viewer);
    return E_OK;
}
void close_viewer(viewer_t *viewer)
{
    _clrscr();
    goto_rowcol(0,0);
    pthread_join(viewer->tid,NULL);
    close(viewer->fd_in);
    close(viewer->fd_out);
}

static inline void print_board(board_t board,uint8_t row,uint8_t col) {
    int i,j;
    for(i=0; i<4; i++) {
        goto_rowcol(row+i,col);
        for(j=0; j<4; j++) {
            uint8_t powerVal = (board) & 0xf;
            printf("%6u", (powerVal == 0) ? 0 : 1 << powerVal);
            board >>= 4;
        }
    }
}

static inline int print_boards_all(viewer_t *viewer)
{
    char cmd='b',buf[1024];
    int i;
    while(read(viewer->fd_out,buf,sizeof(buf))>0);
    int rc=write(viewer->fd_in,&cmd,sizeof(cmd));
    if(rc<=0){
        return E_FILEIO;
    }
    uint8_t retry=8;
    do{
        usleep(10);
        rc=read(viewer->fd_out,buf,sizeof(buf)-1);
    }while(viewer->running && rc<0 && errno==EAGAIN && retry--);
    if(rc<=0){
        return E_FILEIO;
    }
    buf[rc]='\0';
    
    goto_rowcol(0,0);
    char *p_start=buf,*p_end;
    for(i=0; i<viewer->thread_count; i++){
        p_end=strchr(p_start,'\n');
        if(p_end!=NULL){
            *p_end='\0';
        }
        uint32_t moveno,score,idx;
        board_t board;
        sscanf(p_start,"%u,%u,%u,%lx",&idx,&moveno,&score,&board);
        if(p_end!=NULL){
            p_start=p_end+1;
        }else{
            break;
        }
        goto_rowcol(i*6,0);
        printf("Move:%-5u Score:%u",moveno,score);
        print_board(board,i*6+1,0);
    }
    return E_OK;
}
void *viewer_thread(void *data)
{
    viewer_t *viewer=(viewer_t*)data;
    viewer->running=true;
    _clrscr();
    while(viewer->running){
        print_boards_all(viewer);
        usleep(10000);
    }
    return NULL;
}

int viewer2048(const char *pipe_in,const char *pipe_out)
{
    viewer_t viewer;
    if(init_viewer(&viewer,pipe_in,pipe_out)!=E_OK){
        return 1;
    }
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask,SIGINT);
    sigaddset(&mask,SIGQUIT);
    sigaddset(&mask,SIGPIPE);
    sigprocmask(SIG_BLOCK,&mask,NULL);
    while(viewer.running) {
	    int signal;
        sigwait(&mask,&signal);
        if(signal==SIGINT || signal==SIGQUIT){
            viewer.running=false;
        }
        sleep(0);
    }
    close_viewer(&viewer);
    return 0;
}
