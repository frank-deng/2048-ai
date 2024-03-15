#include <stdlib.h>
#include <errno.h>
#include "worker.h"
#include "fileio.h"

static table_data_t table_data;

// For game play
static inline board_t draw_tile(rand_t *rand) {
    return (getRandom(rand) & 1) ? 2 : 1;
}
static inline board_t insert_tile_rand(rand_t *rand, board_t board, board_t tile) {
    int index = getRandom(rand) % (count_empty(board));
    board_t tmp = board;
    while (true) {
        while ((tmp & 0xf) != 0) {
            tmp >>= 4;
            tile <<= 4;
        }
        if (index == 0) break;
        --index;
        tmp >>= 4;
        tile <<= 4;
    }
    return board | tile;
}
void init_game(thread_data_t *thread_data)
{
    rand_t *rand=&thread_data->rand;
    board_t board = (draw_tile(rand) << (4 * (getRandom(rand) % 16)));
    board = insert_tile_rand(rand, board, draw_tile(rand));
    pthread_rwlock_wrlock(&thread_data->rwlock);
    thread_data->moveno = 0;
    thread_data->scoreoffset = 0;
    thread_data->board = board;
    pthread_rwlock_unlock(&thread_data->rwlock);
}
int play_game(table_data_t *table, thread_data_t *thread_data)
{
    pthread_rwlock_rdlock(&thread_data->rwlock);
    board_t board = thread_data->board;
    pthread_rwlock_unlock(&thread_data->rwlock);
    bool playing=true;
    while(thread_data->worker->running && playing) {
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
        board_t tile=draw_tile(&thread_data->rand);
        board=insert_tile_rand(&thread_data->rand,newboard,tile);
        
        pthread_rwlock_wrlock(&thread_data->rwlock);
        (thread_data->moveno)++;
        if (tile == 2) {
            (thread_data->scoreoffset) += 4;
        }
        thread_data->board = board;
        pthread_rwlock_unlock(&thread_data->rwlock);
    }
    return playing;
}
void* thread_main(void *data){
    thread_data_t *thread_data = (thread_data_t*)data;
    while (thread_data->worker->running) {
        if (!play_game(thread_data->worker->table_data, thread_data)) {
            write_log(thread_data);
            init_game(thread_data);
        }
    }
    return NULL;
}

worker_t *worker_start(worker_param_t *param)
{
    worker_t *worker = (worker_t*)calloc(1,sizeof(worker_t)+sizeof(thread_data_t)*param->thread_count);
    if(NULL==worker){
        fprintf(stderr,"malloc failed\n");
        return NULL;
    }
    worker->fileinfo.log_path=param->log_path;
    worker->fileinfo.snapshot_path=param->snapshot_path;
    worker->fileinfo.socket_path=param->socket_path;
    int rc=init_files(&worker->fileinfo);
    if(rc!=E_OK){
        free(worker);
        return NULL;
    }
    init_tables(&table_data);
    worker->thread_count=param->thread_count;
    worker->table_data=&table_data;
    pthread_mutex_init(&(worker->log_mutex), NULL);
    int i;
    for (i = 0; i < worker->thread_count; i++) {
        thread_data_t *thread_data=&(worker->thread_data[i]);
        thread_data->worker=worker;
        initRandom(&thread_data->rand, unif_random(RANDOM_MAX));
        pthread_rwlock_init(&thread_data->rwlock, NULL);
        init_game(thread_data);
    }
    
    read_snapshot(worker);
    worker->running=true;
    for (i = 0; i < worker->thread_count; i++) {
        thread_data_t *thread_data=&(worker->thread_data[i]);
        pthread_create(&(thread_data->tid), NULL, thread_main, thread_data);
    }
    return worker;
}
void worker_stop(worker_t *worker)
{
    worker->running=false;
    int i;
    for (i = 0; i < worker->thread_count; i++) {
        thread_data_t *thread_data=&(worker->thread_data[i]);
        pthread_join(thread_data->tid, NULL);
    }
    write_snapshot(worker);
    for (i = 0; i < worker->thread_count; i++) {
        thread_data_t *thread_data=&(worker->thread_data[i]);
        pthread_rwlock_destroy(&thread_data->rwlock);
    }
    pthread_mutex_destroy(&(worker->log_mutex));
    close_files(&worker->fileinfo);
    free(worker);
}
