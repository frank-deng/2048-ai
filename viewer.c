#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <signal.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <termios.h>
#include "util.h"
#include "viewer.h"

typedef uint64_t board_t;
typedef struct{
    int fd_in;
    int fd_out;
    volatile bool running;
    volatile bool refresh;
    uint16_t cols;
    uint16_t thread_count;
    struct termios flags_orig;
}viewer_t;

static inline void _clrscr()
{
    printf("\033[2J");
}
static inline void goto_rowcol(uint8_t row, uint8_t col)
{
    printf("\033[%u;%uH",row+1,col+1);
}
static inline int _kbhit() {
    int bytesWaiting;
    ioctl(STDIN_FILENO, FIONREAD, &bytesWaiting);
    return bytesWaiting;
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
static inline uint16_t get_columns()
{
    struct winsize size;
    ioctl(STDIN_FILENO,TIOCGWINSZ,&size);
    return size.ws_col;
}
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
    viewer->refresh=true;
    viewer->cols=get_columns();
    int res=get_thread_count(viewer,&viewer->thread_count);
    if(res!=E_OK){
        close(viewer->fd_in);
        close(viewer->fd_out);
        return res;
    }
    
    // Disable echo, set stdin non-blocking for kbhit works
    struct termios flags;
    tcgetattr(STDIN_FILENO, &flags);
    viewer->flags_orig=flags;
    flags.c_lflag &= ~ICANON; //Make stdin non-blocking
    flags.c_lflag &= ~ECHO;   //Turn off echo
    flags.c_lflag |= ECHONL;  //Turn off echo
    tcsetattr(STDIN_FILENO, TCSANOW, &flags);
    setbuf(stdin, NULL);
    return E_OK;
}
void close_viewer(viewer_t *viewer)
{
    tcsetattr(STDIN_FILENO, TCSANOW, &viewer->flags_orig);
    close(viewer->fd_in);
    close(viewer->fd_out);
}

static inline void print_board(board_t board,uint8_t row,uint8_t col) {
    int i,j;
    for(i=0; i<4; i++) {
        goto_rowcol(row+i,col);
        for(j=0; j<4; j++) {
            uint8_t powerVal = (board) & 0xf;
            if(powerVal == 0){
                printf("    . ");
            }else{
                printf("%5u ", 1<<powerVal);
            }
            board >>= 4;
        }
    }
}
#define BOARD_WIDTH 25
#define BOARD_HEIGHT 6
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
    uint16_t row=0,col=0;
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
        goto_rowcol(row,col);
        printf("Move:%-5u Score:%u",moveno,score);
        print_board(board,row+1,col);
        col+=BOARD_WIDTH;
        if((col+BOARD_WIDTH)>=viewer->cols){
            col=0;
            row+=BOARD_HEIGHT;
        }
    }
    return E_OK;
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
    sigaddset(&mask,SIGTERM);
    sigaddset(&mask,SIGWINCH);
    sigprocmask(SIG_BLOCK,&mask,NULL);
    while(viewer.running) {
        if(viewer.refresh){
            viewer.refresh=false;
            _clrscr();
        }
        print_boards_all(&viewer);
        
        struct timespec timeout={0,1};
        siginfo_t info;
        int signal=sigtimedwait(&mask,&info,&timeout);
        switch(signal){
            case SIGINT:
            case SIGQUIT:
            case SIGTERM:
                viewer.running=false;
            break;
            case SIGWINCH:
                viewer.cols=get_columns();
                viewer.refresh=true;
            break;
        }
        
        char ch='\0';
        if(_kbhit()){
            ch=getchar();
        }
        switch(ch){
            case 'q':
            case 'Q':
                viewer.running=false;
            break;
        }
	    usleep(100000);
    }
    _clrscr();
    goto_rowcol(0,0);
    close_viewer(&viewer);
    return 0;
}
