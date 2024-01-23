#include <stdlib.h>
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
void* thread_snapshot(void *data){
    worker_t *worker = (worker_t*)data;
    while(worker->running){
        write_snapshot(worker);
        sleep(1);
    }
    return NULL;
}

static void output_thread_count(int fd,worker_t *worker)
{
    char buf[12];
    snprintf(buf,sizeof(buf),"%u\n",worker->thread_count);
    write(fd,buf,strlen(buf));
}
static void output_board_all(int fd,worker_t *worker)
{
    char buf[128];
    uint16_t i;
    for (i = 0; i < worker->thread_count; i++) {
        thread_data_t *thread_data=&(worker->thread_data[i]);
        pthread_rwlock_rdlock(&(thread_data->rwlock));
        board_t board=thread_data->board;
        uint32_t score_offset=thread_data->scoreoffset;
        uint32_t moveno=thread_data->moveno;
        pthread_rwlock_unlock(&(thread_data->rwlock));
        uint32_t score=score_board(worker->table_data,board)-score_offset;
        snprintf(buf,sizeof(buf),"%u,%u,%u,%016lx\n",i,moveno,score,board);
        write(fd,buf,strlen(buf));
    }
}
#define DELAY_US (100)
void* thread_pipe(void *data){
    worker_t *worker = (worker_t*)data;
    int pipe_fd=open(worker->pipe_path,O_RDWR|O_NONBLOCK);
    if(pipe_fd<0){
        fprintf(stderr,"Open pipe failed.\n");
        return NULL;
    }
    char cmd;
    while(worker->running){
        usleep(DELAY_US);
        cmd='\0';
        if(read(pipe_fd,&cmd,sizeof(cmd))<sizeof(cmd)){
            continue;
        }
        switch(cmd){
            case 'T':
            case 't':
            	output_thread_count(pipe_fd,worker);
            break;
            case 'B':
            case 'b':
                output_board_all(pipe_fd,worker);
            break;
        }
    }
    if(pipe_fd>=0){
        close(pipe_fd);
    }
    return NULL;
}
worker_t *worker_init(uint16_t thread_count, const char *log_path, const char *snapshot_path, const char *pipe_path)
{
    worker_t *worker = (worker_t*)malloc(sizeof(worker_t)+sizeof(thread_data_t)*thread_count);
    if(init_files(&worker->fp_log,&worker->fp_snapshot,log_path,snapshot_path)!=E_OK){
        free(worker);
        return NULL;
    }
    if(init_pipe(pipe_path) < 0){
        close_files(worker->fp_log,worker->fp_snapshot);
        free(worker);
        return NULL;
    }
    worker->pipe_path=pipe_path;
    init_tables(&table_data);
    worker->thread_count=thread_count;
    worker->table_data=&table_data;
    pthread_mutex_init(&(worker->log_mutex), NULL);
    int i;
    for (i = 0; i < thread_count; i++) {
        thread_data_t *thread_data=&(worker->thread_data[i]);
        thread_data->worker=worker;
        initRandom(&thread_data->rand, unif_random(RANDOM_MAX));
        pthread_rwlock_init(&thread_data->rwlock, NULL);
        init_game(thread_data);
    }
    read_snapshot(worker);
    return worker;
}
void worker_start(worker_t *worker)
{
    int i;
    worker->running=true;
    for (i = 0; i < worker->thread_count; i++) {
        thread_data_t *thread_data=&(worker->thread_data[i]);
        pthread_create(&(thread_data->tid), NULL, thread_main, thread_data);
    }
    pthread_create(&(worker->tid_snapshot), NULL, thread_snapshot, worker);
    pthread_create(&(worker->tid_pipe), NULL, thread_pipe, worker);
}
void worker_stop(worker_t *worker)
{
    int i;
    worker->running=false;
    for (i = 0; i < worker->thread_count; i++) {
        thread_data_t *thread_data=&(worker->thread_data[i]);
        pthread_join(thread_data->tid, NULL);
    }
    pthread_join(worker->tid_snapshot,NULL);
    pthread_join(worker->tid_pipe,NULL);
    write_snapshot(worker);
}
void worker_close(worker_t *worker)
{
    int i;
    for (i = 0; i < worker->thread_count; i++) {
        thread_data_t *thread_data=&(worker->thread_data[i]);
        pthread_rwlock_destroy(&thread_data->rwlock);
    }
    pthread_mutex_destroy(&(worker->log_mutex));
    close_pipe(worker->pipe_path);
    close_files(worker->fp_log,worker->fp_snapshot);
    free(worker);
}
