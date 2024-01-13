#include <signal.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/sysinfo.h>
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
static char *filename_log;
static table_data_t table_data;

int write_log(char *fpath, pthread_rwlock_t *stat_rwlock, game_state_t *state)
{
    if(NULL == fpath){
        return -1;
    }
    FILE *fp = fopen(fpath, "a");
	if (NULL==fp) {
        return -1;
    }
    game_state_t state_tmp;
    pthread_rwlock_rdlock(stat_rwlock);
    state_tmp = *state;
    pthread_rwlock_unlock(stat_rwlock);
    pthread_mutex_lock(&log_mutex);
	fprintf(fp, "%lu,%lu,%u,%016lx\n",
		state_tmp.moveno,
		state_tmp.score - state_tmp.scoreoffset,
		1 << get_max_rank(state_tmp.board),
		state_tmp.board
	);
    pthread_mutex_unlock(&log_mutex);
	fclose(fp);
    return 0;
}
int read_snapshot(char *fpath, int proc_cnt, thread_data_t *thread_data)
{
    if(NULL == fpath){
        return -1;
    }
    FILE *fp = fopen(fpath, "r");
	if (NULL == fp){
        return -1;
    }
    int i;
	for (i = 0; i < proc_cnt; i++) {
        pthread_rwlock_wrlock(&(thread_data[i].stat_rwlock));
		fscanf(fp, "%lu,%lu,%lu,%016lx",
			&(thread_data[i].stat.moveno),
			&(thread_data[i].stat.score),
			&(thread_data[i].stat.scoreoffset),
			&(thread_data[i].stat.board)
		);
        pthread_rwlock_unlock(&(thread_data[i].stat_rwlock));
	}
	fclose(fp);
    return 0;
}
int write_snapshot(char *fpath, int proc_cnt, thread_data_t *thread_data)
{
    if(NULL == fpath){
        return -1;
    }
    FILE *fp = fopen(fpath, "w");
	if (NULL == fp){
        return -1;
    }
    int i;
	for (i = 0; i < proc_cnt; i++) {
        pthread_rwlock_rdlock(&(thread_data[i].stat_rwlock));
		fprintf(fp, "%lu,%lu,%lu,%016lx\n",
			thread_data[i].stat.moveno,
			thread_data[i].stat.score,
			thread_data[i].stat.scoreoffset,
			thread_data[i].stat.board
		);
        pthread_rwlock_unlock(&(thread_data[i].stat_rwlock));
	}
	fclose(fp);
    return 0;
}
int play_game(table_data_t *table, pthread_rwlock_t *stat_rwlock, game_state_t *game_state) {
    pthread_rwlock_rdlock(stat_rwlock);
    board_t board = game_state->board;
    pthread_rwlock_unlock(stat_rwlock);
    while(running) {
        int move = find_best_move(table, board);
        if(move < 0){
			return 0;
		}
        board_t newboard = execute_move(table, move, board);
        if(newboard == board) {
            fprintf(stderr, "Illegal move!\n");
			abort();
        }
        board_t tile = draw_tile();
        board = insert_tile_rand(newboard, tile);
        
        pthread_rwlock_wrlock(stat_rwlock);
        (game_state->moveno)++;
        if (tile == 2) {
			(game_state->scoreoffset) += 4;
		}
        game_state->score = (uint64_t)score_board(table, board);
        game_state->board = board;
        pthread_rwlock_unlock(stat_rwlock);
    }
	return 1;
}
void* thread_main(void *data){
    thread_data_t *thread_data = (thread_data_t*)data;
	game_state_t* game_state = &(thread_data->stat);
    pthread_rwlock_t* game_state_rwlock = &(thread_data->stat_rwlock);
	while (running) {
		if (!play_game(&table_data, game_state_rwlock, game_state)) {
            write_log(filename_log, game_state_rwlock, game_state);
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
int main(int argc, char *argv[]) {
	int i;
	if (argc < 3) {
		fprintf(stderr, "Usage: %s LOGFILE STATEFILE\n", argv[0]);
		return 1;
	}
	char *filename_snapshot = argv[2];
	filename_log = argv[1];
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
    read_snapshot(filename_snapshot, proc_cnt, thread_data);
	for (i = 0; i < proc_cnt; i++) {
		pthread_create(&(thread_data[i].tid), NULL, thread_main, &(thread_data[i]));
	}
    while(running) {
        sleep(1);
        write_snapshot(filename_snapshot, proc_cnt, thread_data);
    }
	for (i = 0; i < proc_cnt; i++) {
		pthread_join(thread_data[i].tid, NULL);
	}
	write_snapshot(filename_snapshot, proc_cnt, thread_data);
    for (i = 0; i < proc_cnt; i++) {
		pthread_rwlock_destroy(&(thread_data[i].stat_rwlock));
	}
	free(thread_data);
	signal(SIGINT, SIG_DFL);
	signal(SIGQUIT, SIG_DFL);
	signal(SIGUSR1, SIG_DFL);
	return 0;
}
