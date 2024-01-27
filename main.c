#include <signal.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/sysinfo.h>
#include <sys/stat.h>
#include <unistd.h>
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
    char *app_name_last=strrchr(app_name,'/');
    if(NULL!=app_name_last){
        app_name=app_name_last+1;
    }
    fprintf(stderr,"Usage: %s [-h] [-d] [-n threads]\n",app_name);
}
const char *getfromenv(const char *key,const char *defval)
{
    char *res=getenv(key);
    if(NULL==res){
        return defval;
    }
    return res;
}
int do_stop_daemon(bool daemon_running,const char *pipe_in,const char *pipe_out){
    if(!daemon_running){
        fprintf(stderr,"2048 daemon is not running.\n");
        return 1;
    }
    int fd_in=open(pipe_in,O_RDWR|O_NONBLOCK);
    if(fd_in<0){
        fprintf(stderr,"Failed to communicate with 2048 daemon.\n");
        return 1;
    }
    int rc=0;
    char cmd='q';
    if(write(fd_in,&cmd,sizeof(cmd))<sizeof(cmd)){
        fprintf(stderr,"Failed to stop 2048 daemon.\n");
        rc=1;
    }
    close(fd_in);
    
    // Check whether daemon stopped
    time_t t0=time(NULL),t;
    do{
        t=time(NULL);
        struct stat stat_in,stat_out;
        if(stat(pipe_in,&stat_in)!=0 && stat(pipe_out,&stat_out)!=0){
            break;
        }
    }while(t-t0 < 20);
    if(t-t0 >= 20){
        fprintf(stderr,"Timeout.\n");
        rc=1;
    }else{
        fprintf(stderr,"2048 daemon stopped.\n");
    }
    return rc;
}
int main(int argc, char *argv[]) {
    uint16_t proc_cnt = get_nprocs();
    char *filename_snapshot=getfromenv(ENV_SNAPSHOT_FILE,DEFAULT_SNAPSHOT_FILE);
    char *filename_log=getfromenv(ENV_LOG_FILE,DEFAULT_LOG_FILE);
    char *pipe_in=getfromenv(ENV_PIPE_IN,DEFAULT_PIPE_IN);
    char *pipe_out=getfromenv(ENV_PIPE_OUT,DEFAULT_PIPE_OUT);
    bool viewer=true;
    bool stop_daemon=false;
    unsigned char opt;
    while((opt=getopt(argc,argv,"hdsn:")) != 0xff){
        switch(opt){
            case 'd':
            	viewer=false;
            break;
            case 's':
            	stop_daemon=true;
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
    
    bool daemon_running=test_running(filename_log,filename_snapshot);
    if(stop_daemon){
        return do_stop_daemon(daemon_running,pipe_in,pipe_out);
    }
    if(daemon_running){
        if(viewer){
            return viewer2048(pipe_in,pipe_out);
        }else{
            fprintf(stderr,"2048 daemon is already running.\n");
            return 1;
        }
    }
    pid_t pid=fork();
    if(0!=pid){
        sleep(1);
        if(!test_running(filename_log,filename_snapshot)){
            fprintf(stderr,"Failed to start 2048 daemon.\n");
            return 1;
        }
        if(viewer){
            return viewer2048(pipe_in,pipe_out);
        }else{
            fprintf(stderr,"2048 daemon started.\n");
            return 0;
        }
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
