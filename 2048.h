#ifndef __2048_h__
#define __2048_h__

#include <stdint.h>
#include "util.h"

typedef uint64_t board_t;
typedef uint16_t row_t;

#define ROW_MASK (0xFFFFULL)
#define COL_MASK (0x000F000F000F000FULL)

/* Move tables. Each row or compressed column is mapped to (oldrow^newrow) assuming row/col 0.
 *
 * Thus, the value is 0 if there is no move, and otherwise equals a value that can easily be
 * xor'ed into the current board state to update the board. */
typedef struct {
	row_t row_left_table [65536];
	row_t row_right_table[65536];
	board_t col_up_table[65536];
	board_t col_down_table[65536];
	float heur_score_table[65536];
	float score_table[65536];
} table_data_t;

static inline board_t unpack_col(row_t row) {
    board_t tmp = row;
    return (tmp | (tmp << 12ULL) | (tmp << 24ULL) | (tmp << 36ULL)) & COL_MASK;
}
static inline row_t reverse_row(row_t row) {
    return (row >> 12) | ((row >> 4) & 0x00F0)  | ((row << 4) & 0x0F00) | (row << 12);
}

static inline void print_board(board_t board) {
    int i,j;
    for(i=0; i<4; i++) {
        for(j=0; j<4; j++) {
            uint8_t powerVal = (board) & 0xf;
            printf("%6u", (powerVal == 0) ? 0 : 1 << powerVal);
            board >>= 4;
        }
        printf("\n");
    }
    printf("\n");
}

/*
Transpose rows/columns in a board:
   0123       048c
   4567  -->  159d
   89ab       26ae
   cdef       37bf
*/
static inline board_t transpose(board_t x)
{
    board_t a1 = x & 0xF0F00F0FF0F00F0FULL;
    board_t a2 = x & 0x0000F0F00000F0F0ULL;
    board_t a3 = x & 0x0F0F00000F0F0000ULL;
    board_t a = a1 | (a2 << 12) | (a3 >> 12);
    board_t b1 = a & 0xFF00FF0000FF00FFULL;
    board_t b2 = a & 0x00FF00FF00000000ULL;
    board_t b3 = a & 0x00000000FF00FF00ULL;
    return b1 | (b2 >> 24) | (b3 << 24);
}

// Count the number of empty positions (= zero nibbles) in a board.
// Precondition: the board cannot be fully empty.
static inline uint8_t count_empty(board_t x)
{
    x |= (x >> 2) & 0x3333333333333333ULL;
    x |= (x >> 1);
    x = ~x & 0x1111111111111111ULL;
    // At this point each nibble is:
    //  0 if the original nibble was non-zero
    //  1 if the original nibble was zero
    // Next sum them all
    x += x >> 32;
    x += x >> 16;
    x += x >>  8;
    x += x >>  4; // this can overflow to the next nibble if there were 16 empty positions
    return x & 0xf;
}

/* Execute a move. */
static inline board_t execute_move(table_data_t *table, int move, board_t board) {
    board_t ret = board, t;
    switch(move) {
		case 0: // up
			t = transpose(board);
			ret ^= (table->col_up_table)[(t >>  0) & ROW_MASK] <<  0;
			ret ^= (table->col_up_table)[(t >> 16) & ROW_MASK] <<  4;
			ret ^= (table->col_up_table)[(t >> 32) & ROW_MASK] <<  8;
			ret ^= (table->col_up_table)[(t >> 48) & ROW_MASK] << 12;
			return ret;
		case 1: // down
			t = transpose(board);
			ret ^= (table->col_down_table)[(t >>  0) & ROW_MASK] <<  0;
			ret ^= (table->col_down_table)[(t >> 16) & ROW_MASK] <<  4;
			ret ^= (table->col_down_table)[(t >> 32) & ROW_MASK] <<  8;
			ret ^= (table->col_down_table)[(t >> 48) & ROW_MASK] << 12;
			return ret;
		case 2: // left
			ret ^= (board_t)((table->row_left_table)[(board >>  0) & ROW_MASK]) <<  0;
			ret ^= (board_t)((table->row_left_table)[(board >> 16) & ROW_MASK]) << 16;
			ret ^= (board_t)((table->row_left_table)[(board >> 32) & ROW_MASK]) << 32;
			ret ^= (board_t)((table->row_left_table)[(board >> 48) & ROW_MASK]) << 48;
			return ret;
		case 3: // right
			ret ^= (board_t)((table->row_right_table)[(board >>  0) & ROW_MASK]) <<  0;
			ret ^= (board_t)((table->row_right_table)[(board >> 16) & ROW_MASK]) << 16;
			ret ^= (board_t)((table->row_right_table)[(board >> 32) & ROW_MASK]) << 32;
			ret ^= (board_t)((table->row_right_table)[(board >> 48) & ROW_MASK]) << 48;
			return ret;
		default:
			return ~0ULL;
    }
}

static inline uint8_t get_max_rank(board_t board) {
    uint8_t maxrank = 0;
    while (board) {
        maxrank = max(maxrank, (uint8_t)(board & 0xf));
        board >>= 4;
    }
    return maxrank;
}

// Calculate score
static inline float score_helper(board_t board, const float* table) {
    return table[(board >>  0) & ROW_MASK] +
           table[(board >> 16) & ROW_MASK] +
           table[(board >> 32) & ROW_MASK] +
           table[(board >> 48) & ROW_MASK];
}

// score a single board actually (adding in the score from spawned 4 tiles)
static inline float score_board(table_data_t *table, board_t board) {
    return score_helper(board, table->score_table);
}

// For game play
static inline board_t draw_tile() {
    return (unif_random(1) & 1) ? 2 : 1;
}

static inline board_t insert_tile_rand(board_t board, board_t tile) {
    int index = unif_random(count_empty(board));
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

static inline board_t initial_board() {
    board_t board = draw_tile() << (4 * unif_random(16));
    return insert_tile_rand(board, draw_tile());
}

#ifdef __cplusplus
extern "C" {
#endif

void init_tables(table_data_t *table);
int find_best_move(table_data_t *table, board_t board);

#ifdef __cplusplus
}
#endif

#endif
