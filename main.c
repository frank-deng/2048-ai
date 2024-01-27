#include <signal.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/sysinfo.h>
#include <getopt.h>
#include "2048.h"
#include "fileio.h"
#include "worker.h"
#include "viewer.h"

#define ENV_SNAPSHOT_FILE ("RUN2048_SNAPSHOT_FILE")
#define ENV_LOG_FILE ("RUN2048_LOG_FILE")
#define ENV_PIPE_IN ("RUN2048_PIPE_IN")
#define ENV_PIPE_OUT ("RUN2048_PIPE_OUT")

#define DEFAULT_SNAPSHOT_FILE ("2048.snapshot")
#define DEFAULT_LOG_FILE ("2048.log")
#define DEFAULT_PIPE_IN (".2048.in")
#define DEFAULT_PIPE_OUT (".2048.out")

void print_help(const char *app_name){
    fprintf(stderr,"Daemon: %s [-n num_of_threads] &\nViewer: %s -v\n",app_name,app_name);
    fprintf(stderr,"After daemon started, send signal SIGINT to the daemon process to stop it gracefully.\n");
}
const char *getfromenv(const char *key,const char *defval)
{
    char *res=getenv(key);
    if(NULL==res){
        return defval;
    }
    return res;
}
int main(int argc, char *argv[]) {
    uint16_t proc_cnt = get_nprocs();
    char *filename_snapshot=getfromenv(ENV_SNAPSHOT_FILE,DEFAULT_SNAPSHOT_FILE);
    char *filename_log=getfromenv(ENV_LOG_FILE,DEFAULT_LOG_FILE);
    char *pipe_in=getfromenv(ENV_PIPE_IN,DEFAULT_PIPE_IN);
    char *pipe_out=getfromenv(ENV_PIPE_OUT,DEFAULT_PIPE_OUT);
    bool viewer=false;
    unsigned char opt;
    while((opt=getopt(argc,argv,"hvn:")) != 0xff){
        switch(opt){
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
    
    worker_param_t param={
        .thread_count=proc_cnt,
        .log_path=filename_log,
        .snapshot_path=filename_snapshot,
        .pipe_in=pipe_in,
        .pipe_out=pipe_out
    };
    worker_t *worker=worker_start(&param);
    if(NULL==worker){
        return 1;
    }
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask,SIGINT);
    sigaddset(&mask,SIGQUIT);
    sigaddset(&mask,SIGPIPE);
    sigprocmask(SIG_BLOCK,&mask,NULL);
    time_t t0=time(NULL);
    while(worker->running) {
        struct timespec timeout={0,1};
        siginfo_t info;
        int signal=sigtimedwait(&mask,&info,&timeout);
        switch(signal){
            case SIGINT:
            case SIGQUIT:
            case SIGTERM:
                worker->running=false;
            break;
        }
        time_t t=time(NULL);
        if((t-t0)>=1){
            t0=t;
            write_snapshot(worker);
        }
        usleep(10000);
    }
    worker_stop(worker);
    return 0;
}
