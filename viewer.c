#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <signal.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/file.h>
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
    bool term_init;
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
static inline uint16_t get_columns()
{
    struct winsize size;
    ioctl(STDIN_FILENO,TIOCGWINSZ,&size);
    return size.ws_col;
}

void close_viewer(viewer_t *viewer);
int init_viewer(viewer_t *viewer, const char *pipe_in, const char *pipe_out)
{
    int rc=E_OK;
    viewer->fd_in=viewer->fd_out=-1;
    viewer->term_init=false;
    
    viewer->fd_in=open(pipe_in,O_RDWR|O_NONBLOCK);
    if(viewer->fd_in<0){
        fprintf(stderr,"Failed to open pipe %s, maybe daemon is not running.\n",pipe_in);
        rc=E_FILEIO;
        goto error_exit;
    }
    
    viewer->fd_out=open(pipe_out,O_RDWR|O_NONBLOCK);
    if(viewer->fd_out<0){
        fprintf(stderr,"Failed to open pipe %s\n",pipe_in);
        rc=E_FILEIO;
        goto error_exit;
    }
    
    viewer->running=true;
    viewer->refresh=true;
    viewer->cols=get_columns();
    
    // Disable echo, set stdin non-blocking for kbhit works
    struct termios flags;
    tcgetattr(STDIN_FILENO, &flags);
    viewer->flags_orig=flags;
    flags.c_lflag &= ~ICANON; //Make stdin non-blocking
    flags.c_lflag &= ~ECHO;   //Turn off echo
    flags.c_lflag |= ECHONL;  //Turn off echo
    tcsetattr(STDIN_FILENO, TCSANOW, &flags);
    setbuf(stdin, NULL);
    viewer->term_init=true;
    return E_OK;
error_exit:
    close_viewer(viewer);
    return rc;
}
void close_viewer(viewer_t *viewer)
{
    if(viewer->term_init){
        tcsetattr(STDIN_FILENO, TCSANOW, &viewer->flags_orig);
    }
    if(viewer->fd_in>=0){
        flock(viewer->fd_in,LOCK_UN|LOCK_NB);
        close(viewer->fd_in);
    }
    if(viewer->fd_out>=0){
        flock(viewer->fd_out,LOCK_UN|LOCK_NB);
        close(viewer->fd_out);
    }
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
                printf("%5u ", ((uint16_t)1)<<powerVal);
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
    uint16_t retry=2000;
    do{
        usleep(1000);
        rc=read(viewer->fd_out,buf,sizeof(buf)-1);
    }while(viewer->running && rc<0 && errno==EAGAIN && retry--);
    if(rc<=0 && errno==EAGAIN){
        return E_TIMEOUT;
    }else if(rc<=0){
        return E_FILEIO;
    }
    buf[rc]='\0';
    
    // Get thread count
    char *p_start=buf,*p_end=strchr(p_start,'\n');
    if(p_end==NULL){
        return E_INVAL;
    }
    *p_end='\0';
    uint16_t thread_count=strtoul(p_start,NULL,0);
    p_start=p_end+1;
    
    // Display boards
    goto_rowcol(0,0);
    uint16_t row=0,col=0;
    for(i=0; i<thread_count; i++){
        p_end=strchr(p_start,'\n');
        if(p_end!=NULL){
            *p_end='\0';
        }
        uint32_t moveno,score,idx;
        board_t board;
        sscanf(p_start,"%u,%u,%u,%llx",&idx,&moveno,&score,&board);
        goto_rowcol(row,col);
        printf("Move:%-5u Score:%-6u",moveno,score);
        print_board(board,row+1,col);
        col+=BOARD_WIDTH;
        if((col+BOARD_WIDTH)>=viewer->cols){
            col=0;
            row+=BOARD_HEIGHT;
        }
        if(p_end!=NULL){
            p_start=p_end+1;
        }else{
            break;
        }
    }
    fflush(stdout);
    return E_OK;
}
viewer_t viewer;
void do_stop_viewer(int signal)
{
    viewer.running=false;
}
void do_refresh_viewer(int signal)
{
    viewer.refresh=true;
}
int viewer2048(const char *pipe_in,const char *pipe_out)
{
    if(init_viewer(&viewer,pipe_in,pipe_out)!=E_OK){
        return 1;
    }
    signal(SIGINT,do_stop_viewer);
    signal(SIGQUIT,do_stop_viewer);
    signal(SIGTERM,do_stop_viewer);
    signal(SIGWINCH,do_refresh_viewer);
    time_t t0=time(NULL);
    int rc_print=E_OK;
    while(viewer.running) {
        if(viewer.refresh){
            viewer.refresh=false;
            viewer.cols=get_columns();
            _clrscr();
            rc_print=print_boards_all(&viewer);
            t0=time(NULL);
        }else{
            time_t t=time(NULL);
            if((t-t0)>=1){
                t0=t;
                rc_print=print_boards_all(&viewer);
            }
        }
        if(rc_print!=E_OK){
            viewer.running=false;
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
	    usleep(1000);
    }
    _clrscr();
    goto_rowcol(0,0);
    close_viewer(&viewer);
    return 0;
}
