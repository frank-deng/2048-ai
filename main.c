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
	game_state_t stat;
} thread_data_t;
static thread_data_t *thread_data;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static int proc_cnt, running = 1, master_running = 1;
static char *filename, *filename_stat;
static table_data_t table_data;

int play_game(table_data_t *table, game_state_t *game_state) {
    board_t board = game_state->board;
	int playing = 1;

    while(running) {
        int move;
        board_t newboard;

        for(move = 0; move < 4; move++) {
            if(execute_move(table, move, board) != board)
                break;
        }
        if(move == 4) {
			playing = 0;
            break; // no legal moves
		}
		//print_board(board);
		(game_state->moveno)++;

        move = find_best_move(table, board);
        if(move < 0) {
			playing = 0;
            break;
		}

        newboard = execute_move(table, move, board);
        if(newboard == board) {
            printf("Illegal move!\n");
			(game_state->moveno)--;
            continue;
        }

        board_t tile = draw_tile();
        if (tile == 2) {
			(game_state->scoreoffset) += 4;
		}
        board = insert_tile_rand(newboard, tile);
    }
	game_state->score = (uint64_t)score_board(table, board);
	game_state->board = board;
	return playing;
}
void* thread_main(void *data){
	FILE *fp;
	game_state_t* game_state = &(((thread_data_t*)data)->stat);
	while (running) {
		if (0 == game_state->moveno){
			game_state->board = initial_board();
			game_state->scoreoffset = 0;
		}
		if (!play_game(&table_data, game_state)) {
			pthread_mutex_lock(&mutex);
			if (fp = fopen(filename, "a")) {
				fprintf(fp, "%llu,%llu,%u,%016llx\n",
					game_state->moveno,
					game_state->score - game_state->scoreoffset,
					1 << get_max_rank(game_state->board),
					game_state->board
				);
				fclose(fp);
			}
			pthread_mutex_unlock(&mutex);
			game_state->moveno = 0;
		}
	}
	return ((void*)0);
}
void action_report(int sig){
	running = 0;
}
void action_quit(int sig){
	running = 0;
	master_running = 0;
}
int main(int argc, char *argv[]) {
	int i, cnt;
	uint64_t dummy;
	uint64_t stat[16] = {
		0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0,
	};
	if (argc < 3) {
		fprintf(stderr, "Usage: %s LOGFILE STATEFILE\n", argv[0], argv[0]);
		return 1;
	}

	filename = argv[1];
	filename_stat = argv[2];
	init_tables(&table_data);

	proc_cnt = get_nprocs();
	signal(SIGINT, action_quit);
	signal(SIGQUIT, action_quit);
	signal(SIGUSR1, action_report);
    thread_data = (thread_data_t*)malloc(sizeof(thread_data_t) * proc_cnt);

	for (i = 0; i < proc_cnt; i++) {
		thread_data[i].stat.moveno = 0;
	}
	FILE *fstat = fopen(filename_stat, "r");
	if (NULL != fstat){
		for (i = 0; i < proc_cnt; i++) {
			fscanf(fstat, "%llu,%llu,%llu,%016llx",
				&(thread_data[i].stat.moveno),
				&dummy,
				&(thread_data[i].stat.scoreoffset),
				&(thread_data[i].stat.board)
			);
		}
		fclose(fstat);
	}

	while (master_running){
		running = 1;
		for (i = 0; i < proc_cnt; i++) {
			pthread_create(&(thread_data[i].tid), NULL, thread_main, &(thread_data[i]));
		}
		for (i = 0; i < proc_cnt; i++) {
			pthread_join(thread_data[i].tid, NULL);
		}

		FILE *fstat = fopen(filename_stat, "w");
		if (NULL != fstat){
			for (i = 0; i < proc_cnt; i++) {
				fprintf(fstat, "%llu,%llu,%llu,%016llx\n",
					thread_data[i].stat.moveno,
					thread_data[i].stat.score,
					thread_data[i].stat.scoreoffset,
					thread_data[i].stat.board
				);
			}
			fclose(fstat);
		}
	}

	free(thread_data);
	signal(SIGINT, SIG_DFL);
	signal(SIGQUIT, SIG_DFL);
	signal(SIGUSR1, SIG_DFL);
	return 0;
}
