#include <signal.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/sysinfo.h>
#include <sys/file.h>
#include <getopt.h>
#include "2048.h"

typedef struct {
	uint64_t moveno;
	uint64_t score;
	uint64_t scoreoffset;
	board_t board;
} game_state_t;
typedef struct {
	pthread_t tid;
    pthread_rwlock_t stat_rwlock;
	game_state_t stat;
} thread_data_t;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool volatile running = true;
static FILE *fp_log=NULL, *fp_snapshot=NULL;
static table_data_t table_data;

void close_files()
{
    if (NULL != fp_log) {
        flock(fileno(fp_log),LOCK_UN|LOCK_NB);
        fclose(fp_log);
    }
    if (NULL != fp_snapshot) {
        flock(fileno(fp_snapshot),LOCK_UN|LOCK_NB);
        fclose(fp_snapshot);
    }
}
int init_files(const char *log_path, const char *snapshot_path)
{
    fp_log=fopen(log_path, "a");
    if(NULL==fp_log){
        fprintf(stderr,"Failed to open log file %s\n",log_path);
        goto error_exit;
    }
    if(flock(fileno(fp_log),LOCK_EX|LOCK_NB)!=0){
        fprintf(stderr,"Failed to lock log file %s, possibly other instance is running.\n",log_path);
        goto error_exit;
    }
    fp_snapshot=fopen(snapshot_path, "rb+");
    if(NULL==fp_snapshot){
        fprintf(stderr,"Failed to open snapshot file %s\n",snapshot_path);
        goto error_exit;
    }
    if(flock(fileno(fp_snapshot),LOCK_EX|LOCK_NB)!=0){
        fprintf(stderr,"Failed to lock snapshot file %s, possibly other instance is running.\n",snapshot_path);
        goto error_exit;
    }
    return 0;
error_exit:
    close_files();
    return -1;
}
int write_log(pthread_rwlock_t *stat_rwlock, game_state_t *state)
{
    game_state_t state_tmp;
    pthread_rwlock_rdlock(stat_rwlock);
    state_tmp = *state;
    pthread_rwlock_unlock(stat_rwlock);
    pthread_mutex_lock(&log_mutex);
	fprintf(fp_log, "%lu,%lu,%u,%016lx\n",
		state_tmp.moveno,
		state_tmp.score - state_tmp.scoreoffset,
		1 << get_max_rank(state_tmp.board),
		state_tmp.board
	);
    pthread_mutex_unlock(&log_mutex);
    fflush(fp_log);
    return 0;
}
int read_snapshot(int proc_cnt, thread_data_t *thread_data)
{
    int i;
	for (i = 0; i < proc_cnt; i++) {
        pthread_rwlock_wrlock(&(thread_data[i].stat_rwlock));
		fscanf(fp_snapshot, "%lu,%lu,%lu,%016lx",
			&(thread_data[i].stat.moveno),
			&(thread_data[i].stat.score),
			&(thread_data[i].stat.scoreoffset),
			&(thread_data[i].stat.board)
		);
        pthread_rwlock_unlock(&(thread_data[i].stat_rwlock));
	}
    return 0;
}
int write_snapshot(int proc_cnt, thread_data_t *thread_data)
{
    fseek(fp_snapshot,0,SEEK_SET);
    ftruncate(fileno(fp_snapshot), 0);
    fseek(fp_snapshot,0,SEEK_SET);
    int i;
	for (i = 0; i < proc_cnt; i++) {
        pthread_rwlock_rdlock(&(thread_data[i].stat_rwlock));
		fprintf(fp_snapshot, "%lu,%lu,%lu,%016lx\n",
			thread_data[i].stat.moveno,
			thread_data[i].stat.score,
			thread_data[i].stat.scoreoffset,
			thread_data[i].stat.board
		);
        pthread_rwlock_unlock(&(thread_data[i].stat_rwlock));
	}
    fflush(fp_snapshot);
    return 0;
}
int play_game(table_data_t *table, pthread_rwlock_t *stat_rwlock, game_state_t *game_state) {
    pthread_rwlock_rdlock(stat_rwlock);
    board_t board = game_state->board;
    pthread_rwlock_unlock(stat_rwlock);
    bool playing=true;
    while(running && playing) {
        int move = find_best_move(table, board);
        if(move < 0){
			playing=false;
            break;
		}
        board_t newboard = execute_move(table, move, board);
        if(newboard == board) {
            fprintf(stderr, "Illegal move!\n");
			abort();
        }
        uint8_t max_rank=get_max_rank(newboard);
        board_t tile = 0;
        
        // Since 32768+32768 cannot be represented well, stop playing when the max number is 32768
        if(max_rank < 0xf){
            tile=draw_tile();
        	board=insert_tile_rand(newboard, tile);
        }else{
            board=newboard;
            playing=false;
        }
        
        pthread_rwlock_wrlock(stat_rwlock);
        (game_state->moveno)++;
        if (tile == 2) {
			(game_state->scoreoffset) += 4;
		}
        game_state->score = (uint64_t)score_board(table, board);
        game_state->board = board;
        pthread_rwlock_unlock(stat_rwlock);
    }
	return playing;
}
void* thread_main(void *data){
    thread_data_t *thread_data = (thread_data_t*)data;
	game_state_t* game_state = &(thread_data->stat);
    pthread_rwlock_t* game_state_rwlock = &(thread_data->stat_rwlock);
	while (running) {
		if (!play_game(&table_data, game_state_rwlock, game_state)) {
            write_log(game_state_rwlock, game_state);
            pthread_rwlock_wrlock(game_state_rwlock);
			game_state->moveno = 0;
            game_state->board = initial_board();
			game_state->score = 0;
            game_state->scoreoffset = 0;
            pthread_rwlock_unlock(game_state_rwlock);
		}
	}
	return NULL;
}
void action_quit(int sig){
	running=false;
}
void print_help(const char *app_name){
    fprintf(stderr,"Usage: %s [-o log_file] [-s snapshot_file]\n",app_name);
}
int main(int argc, char *argv[]) {
	int i;
    char *filename_snapshot="2048.snapshot";
	char *filename_log="2048.log";
    unsigned char opt;
    while((opt=getopt(argc,argv,"ho:s:")) != 0xff){
        switch(opt){
            case 'o':
                filename_log=optarg;
            break;
            case 's':
                filename_snapshot=optarg;
            break;
            default:
                print_help(argv[0]);
                return 1;
            break;
        }
    }
    
    if(init_files(filename_log, filename_snapshot) < 0){
        return 1;
    }
	init_tables(&table_data);
	int proc_cnt = get_nprocs();
	signal(SIGINT, action_quit);
	signal(SIGQUIT, action_quit);
    thread_data_t *thread_data = (thread_data_t*)malloc(sizeof(thread_data_t) * proc_cnt);
	for (i = 0; i < proc_cnt; i++) {
		thread_data[i].stat.moveno = 0;
        thread_data[i].stat.score = 0;
        thread_data[i].stat.scoreoffset = 0;
        thread_data[i].stat.board=initial_board();
        pthread_rwlock_init(&(thread_data[i].stat_rwlock), NULL);
	}
    read_snapshot(proc_cnt, thread_data);
	for (i = 0; i < proc_cnt; i++) {
		pthread_create(&(thread_data[i].tid), NULL, thread_main, &(thread_data[i]));
	}
    while(running) {
        sleep(1);
        write_snapshot(proc_cnt, thread_data);
    }
	for (i = 0; i < proc_cnt; i++) {
		pthread_join(thread_data[i].tid, NULL);
	}
	write_snapshot(proc_cnt, thread_data);
    for (i = 0; i < proc_cnt; i++) {
		pthread_rwlock_destroy(&(thread_data[i].stat_rwlock));
	}
	free(thread_data);
    close_files();
	signal(SIGINT, SIG_DFL);
	signal(SIGQUIT, SIG_DFL);
	signal(SIGUSR1, SIG_DFL);
	return 0;
}
