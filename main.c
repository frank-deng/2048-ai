#include <signal.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/sysinfo.h>
#include <getopt.h>
#include "2048.h"
#include "worker.h"
#include "viewer.h"

void print_help(const char *app_name){
    fprintf(stderr,"Usage: %s [-o log_file] [-s snapshot_file] [-n num_of_threads]\n",app_name);
}
int main(int argc, char *argv[]) {
    uint16_t proc_cnt = get_nprocs();
    char *filename_snapshot="2048.snapshot";
    char *filename_log="2048.log";
    char *pipe_in="2048.in";
    char *pipe_out="2048.out";
    bool viewer=false;
    unsigned char opt;
    while((opt=getopt(argc,argv,"hvo:s:n:")) != 0xff){
        switch(opt){
            case 'o':
                filename_log=optarg;
            break;
            case 's':
                filename_snapshot=optarg;
            break;
            case 'v':
            	viewer=true;
            break;
            case 'n':
                proc_cnt=strtoul(optarg,NULL,10);
                if(proc_cnt<1){
                    print_help(argv[0]);
                	return 1;
                }
            break;
            default:
                print_help(argv[0]);
                return 1;
            break;
        }
    }
    
    if(viewer){
        return viewer2048(pipe_in,pipe_out);
    }
    
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask,SIGINT);
    sigaddset(&mask,SIGQUIT);
    sigaddset(&mask,SIGPIPE);
    sigprocmask(SIG_BLOCK,&mask,NULL);

    worker_t *worker=worker_init(proc_cnt,filename_log,filename_snapshot,pipe_in,pipe_out);
    if(NULL==worker){
        return 1;
    }
    worker_start(worker);
    while(worker->running) {
	    int signal;
        sigwait(&mask,&signal);
        if(signal==SIGINT || signal==SIGQUIT){
            break;
        }
        sleep(0);
    }
    worker_stop(worker);
    worker_close(worker);
    return 0;
}
