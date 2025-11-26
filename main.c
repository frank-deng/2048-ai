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
#define ENV_SOCKET_PATH ("RUN2048_SOCKET_PATH")

#define DEFAULT_SNAPSHOT_FILE ("2048.snapshot")
#define DEFAULT_LOG_FILE ("2048.log")
#define DEFAULT_SOCKET_PATH (".2048-run.socket")

void print_help(const char *app_name){
    char *app_name_last_posix=strrchr(app_name,'/');
    char *app_name_last_win=strrchr(app_name,'\\');
    if(NULL!=app_name_last_posix){
        app_name=app_name_last_posix+1;
    }else if(NULL!=app_name_last_win){
        app_name=app_name_last_win+1;
    }
    fprintf(stderr,"Usage: %s [-h] [-d] [-s] [-n instances]\n",app_name);
    fprintf(stderr,"       -h            Print help.\n");
    fprintf(stderr,"       -d            Start 2048 daemon.\n");
    fprintf(stderr,"       -s            Stop 2048 daemon.\n");
    fprintf(stderr,"       -n instances  Specify instances for running.\n");
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
int do_stop_daemon(bool daemon_running,const char *socket_path){
    if(!daemon_running){
        fprintf(stderr,"2048 daemon is not running.\n");
        return 1;
    }
    
    int fd=socket(PF_UNIX,SOCK_STREAM,0);
    if(fd<0){
        fprintf(stderr,"Failed to init socket.\n");
        return 1;
    }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    
    struct sockaddr_un addr;
    addr.sun_family=AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path)-1);
    if(connect(fd,(struct sockaddr*)&addr,sizeof(addr)) < 0){
        fprintf(stderr,"Failed to open socket %s, maybe daemon is not running.\n",socket_path);
        close(fd);
        return 1;
    }
    
    char cmd='q';
    if(write(fd,&cmd,sizeof(cmd))<sizeof(cmd)){
        fprintf(stderr,"Failed to stop 2048 daemon.\n");
    }
    close(fd);
    if(wait_daemon(false,socket_path,20)!=E_OK){
        fprintf(stderr,"Timeout.\n");
        return 1;
    }
    fprintf(stderr,"2048 daemon stopped.\n");
    return 0;
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
    const char *socket_path=getfromenv(ENV_SOCKET_PATH,DEFAULT_SOCKET_PATH);
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
        return do_stop_daemon(daemon_running,socket_path);
    }
    if(daemon_running){
        if(viewer){
            return viewer2048(socket_path);
        }else{
            fprintf(stderr,"2048 daemon is already running.\n");
            return 1;
        }
    }
    pid_t pid=fork();
    if(0!=pid){
        if(E_OK!=wait_daemon(true,socket_path,20)){
            fprintf(stderr,"Failed to start 2048 daemon.\n");
            return 1;
        }
        if(viewer){
            return viewer2048(socket_path);
        }
        fprintf(stderr,"2048 daemon started.\n");
        return 0;
    }
    
    if(proc_cnt==0){
        proc_cnt=get_cpu_count()/4;
	if(proc_cnt==0){
            proc_cnt=1;
	}
    }
    worker_param_t param={
        .thread_count=proc_cnt,
        .log_path=filename_log,
        .snapshot_path=filename_snapshot,
        .socket_path=socket_path
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
        socket_handler(worker);
        time_t t=time(NULL);
        if((t-t0)>=1){
            t0=t;
            write_snapshot(worker);
        }
        usleep(1000);
    }
    worker_stop(worker);
    return 0;
}

