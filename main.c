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
    char *app_name_last_posix=strrchr(app_name,'/');
    char *app_name_last_win=strrchr(app_name,'\\');
    if(NULL!=app_name_last_posix){
        app_name=app_name_last_posix+1;
    }else if(NULL!=app_name_last_win){
        app_name=app_name_last_win+1;
    }
    fprintf(stderr,"Usage: %s [-h] [-d] [-s] [-n threads]\n",app_name);
    fprintf(stderr,"       -h          Print help.\n");
    fprintf(stderr,"       -d          Start 2048 daemon.\n");
    fprintf(stderr,"       -s          Stop 2048 daemon.\n");
    fprintf(stderr,"       -n threads  Specify threads for running.\n");
}
uint16_t get_cpu_count()
{
    FILE *fp=fopen("/proc/cpuinfo","r");
    if(NULL==fp){
    	fprintf(stderr,"Failed to get cpu count.\n");
	return 1;
    }
    uint16_t res=0;
    char buf[80];
    while(fgets(buf,sizeof(buf),fp)!=NULL){
	if(strncmp(buf,"processor",sizeof("processor")-1)==0){
	    res++;
	}
    }
    return max(res,1);
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
    }
    close(fd_in);
    if(wait_daemon(false,pipe_in,pipe_out,20)!=E_OK){
        fprintf(stderr,"Timeout.\n");
        return 1;
    }
    fprintf(stderr,"2048 daemon stopped.\n");
    return rc;
}

worker_t *worker=NULL;
void do_stop_worker(int signal)
{
    if(NULL==worker){
        return;
    }
    worker->running=false;
}
int main(int argc, char *argv[]) {
    volatile uint16_t proc_cnt = 0;
    const char *filename_snapshot=getfromenv(ENV_SNAPSHOT_FILE,DEFAULT_SNAPSHOT_FILE);
    const char *filename_log=getfromenv(ENV_LOG_FILE,DEFAULT_LOG_FILE);
    const char *pipe_in=getfromenv(ENV_PIPE_IN,DEFAULT_PIPE_IN);
    const char *pipe_out=getfromenv(ENV_PIPE_OUT,DEFAULT_PIPE_OUT);
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
        if(E_OK!=wait_daemon(true,pipe_in,pipe_out,20)){
            fprintf(stderr,"Failed to start 2048 daemon.\n");
            return 1;
        }
        if(viewer){
            return viewer2048(pipe_in,pipe_out);
        }
        fprintf(stderr,"2048 daemon started.\n");
        return 0;
    }
    
    if(proc_cnt==0){
	proc_cnt=get_cpu_count();
    }
    worker_param_t param={
        .thread_count=proc_cnt,
        .log_path=filename_log,
        .snapshot_path=filename_snapshot,
        .pipe_in=pipe_in,
        .pipe_out=pipe_out
    };
    worker=worker_start(&param);
    if(NULL==worker){
        return 1;
    }
    signal(SIGINT,SIG_IGN);
    signal(SIGQUIT,SIG_IGN);
    signal(SIGTERM,do_stop_worker);
    time_t t0=time(NULL);
    while(worker->running) {
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

