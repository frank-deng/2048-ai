#include <sys/file.h>
#include "worker.h"

static table_data_t table_data;
static bool volatile running = true;

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
int write_log(thread_data_t *thread_data)
{
    pthread_rwlock_rdlock(&thread_data->rwlock);
    board_t board=thread_data->board;
    uint64_t score_offset=thread_data->scoreoffset;
    uint32_t moveno=thread_data->moveno;
    pthread_rwlock_unlock(&thread_data->rwlock);

    worker_t *worker=thread_data->worker;
    uint64_t score=score_board(worker->table_data,board)-score_offset;
    uint16_t max_rank=(1<<get_max_rank(board));

    pthread_mutex_lock(&worker->log_mutex);
    fprintf(worker->fp_log,"%u,%u,%u,%016lx\n",moveno,score,max_rank,board);
    fflush(worker->fp_log);
    pthread_mutex_unlock(&worker->log_mutex);
    return E_OK;
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

static void close_files(FILE *fp_log, FILE *fp_snapshot)
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
static int init_files(worker_t *worker, const char *log_path, const char *snapshot_path)
{
    FILE *fp_log=NULL, *fp_snapshot=NULL;
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
        fp_snapshot=fopen(snapshot_path, "wb+");
    }
    if(NULL==fp_snapshot){
        fprintf(stderr,"Failed to open snapshot file %s\n",snapshot_path);
        goto error_exit;
    }
    if(flock(fileno(fp_snapshot),LOCK_EX|LOCK_NB)!=0){
        fprintf(stderr,"Failed to lock snapshot file %s, possibly other instance is running.\n",snapshot_path);
        goto error_exit;
    }
    worker->fp_log=fp_log;
    worker->fp_snapshot=fp_snapshot;
    return E_OK;
error_exit:
    if (NULL != fp_log) {
        flock(fileno(fp_log),LOCK_UN|LOCK_NB);
        fclose(fp_log);
    }
    if (NULL != fp_snapshot) {
        flock(fileno(fp_snapshot),LOCK_UN|LOCK_NB);
        fclose(fp_snapshot);
    }
    close_files(fp_log,fp_snapshot);
    return E_FILEIO;
}
static int read_snapshot(worker_t *worker)
{
    int i;
    for (i = 0; i < worker->thread_count; i++) {
        thread_data_t *thread_data=&(worker->thread_data[i]);
        board_t board;
        uint32_t score_offset;
        uint32_t moveno;
        fscanf(worker->fp_snapshot,"%u,%u,%016lx",&moveno,&score_offset,&board);
        pthread_rwlock_wrlock(&thread_data->rwlock);
        thread_data->moveno=moveno;
        thread_data->scoreoffset=score_offset;
        thread_data->board=board;
        pthread_rwlock_unlock(&thread_data->rwlock);
    }
    return E_OK;
}
static int write_snapshot(worker_t *worker)
{
    fseek(worker->fp_snapshot,0,SEEK_SET);
    ftruncate(fileno(worker->fp_snapshot), 0);
    fseek(worker->fp_snapshot,0,SEEK_SET);
    int i;
    for (i = 0; i < worker->thread_count; i++) {
        thread_data_t *thread_data=&(worker->thread_data[i]);
        pthread_rwlock_rdlock(&(thread_data->rwlock));
        board_t board=thread_data->board;
        uint64_t score_offset=thread_data->scoreoffset;
        uint32_t moveno=thread_data->moveno;
        pthread_rwlock_unlock(&(thread_data->rwlock));
        fprintf(worker->fp_snapshot,"%u,%u,%016lx\n",moveno,score_offset,board);
    }
    fflush(worker->fp_snapshot);
    return E_OK;
}
void* thread_snapshot(void *data){
    worker_t *worker = (worker_t*)data;
    while(worker->running){
    write_snapshot(worker);
    sleep(1);
    }
}

worker_t *worker_init(uint16_t thread_count, const char *log_path, const char *snapshot_path)
{
    worker_t *worker = (worker_t*)malloc(sizeof(worker_t)+sizeof(thread_data_t)*thread_count);
    if(init_files(worker, log_path, snapshot_path)!=E_OK){
        free(worker);
        return NULL;
    }
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
    close_files(worker->fp_log,worker->fp_snapshot);
    free(worker);
}

