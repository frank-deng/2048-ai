/* The fundamental trick: the 4x4 board is represented as a 64-bit word,
 * with each board square packed into a single 4-bit nibble.
 * 
 * The maximum possible board value that can be supported is 32768 (2^15), but
 * this is a minor limitation as achieving 65536 is highly unlikely under normal circumstances.
 * 
 * The space and computation savings from using this representation should be significant.
 * 
 * The nibble shift can be computed as (r,c) -> shift (4*r + c). That is, (0,0) is the LSB.
 */

#include <ctype.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
//#include <algorithm>
#include <unordered_map>
#include <future>
#include "2048.h"

static inline uint8_t count_distinct_tiles(board_t board) {
    uint16_t bitset = 0;
    while (board) {
        bitset |= 1<<(board & 0xf);
        board >>= 4;
    }

    // Don't count empty tiles.
    bitset >>= 1;

    uint8_t count = 0;
    while (bitset) {
        bitset &= bitset - 1;
        count++;
    }
    return count;
}

//store the depth at which the heuristic was recorded as well as the actual heuristic
struct trans_table_entry_t{
    uint8_t depth;
    float heuristic;
};
typedef std::unordered_map<board_t, trans_table_entry_t> trans_table_t;

/* Optimizing the game */
struct eval_state {
    trans_table_t trans_table; // transposition table, to cache previously-seen moves
    int maxdepth;
    int curdepth;
    int cachehits;
    unsigned long moves_evaled;
    int depth_limit;
    eval_state() : maxdepth(0), curdepth(0), cachehits(0), moves_evaled(0), depth_limit(0) {
    }
};

// score over all possible moves
static float score_move_node(table_data_t *table, eval_state &state, board_t board, float cprob);
// score over all possible tile choices and placements
static float score_tilechoose_node(table_data_t *table, eval_state &state, board_t board, float cprob);

// score a single board heuristically
static inline float score_heur_board(table_data_t *table, board_t board) {
    return score_helper(          board , table->heur_score_table) +
           score_helper(transpose(board), table->heur_score_table);
}

// Statistics and controls
// cprob: cumulative probability
// don't recurse into a node with a cprob less than this threshold
static const float CPROB_THRESH_BASE = 0.0001f;
static const int CACHE_DEPTH_LIMIT  = 15;

static float score_tilechoose_node(table_data_t *table, eval_state &state, board_t board, float cprob) {
    if (cprob < CPROB_THRESH_BASE || state.curdepth >= state.depth_limit) {
        state.maxdepth = max(state.curdepth, state.maxdepth);
        return score_heur_board(table, board);
    }
    if (state.curdepth < CACHE_DEPTH_LIMIT) {
        const trans_table_t::iterator &i = state.trans_table.find(board);
        if (i != state.trans_table.end()) {
            trans_table_entry_t entry = i->second;
            /*
            return heuristic from transposition table only if it means that
            the node will have been evaluated to a minimum depth of state.depth_limit.
            This will result in slightly fewer cache hits, but should not impact the
            strength of the ai negatively.
            */
            if(entry.depth <= state.curdepth) {
                state.cachehits++;
                return entry.heuristic;
            }
        }
    }

    int num_open = count_empty(board);
    cprob /= num_open;

    float res = 0.0f;
    board_t tmp = board;
    board_t tile_2 = 1;
    while (tile_2) {
        if ((tmp & 0xf) == 0) {
            res += score_move_node(table, state, board |  tile_2      , cprob * 0.9f) * 0.9f;
            res += score_move_node(table, state, board | (tile_2 << 1), cprob * 0.1f) * 0.1f;
        }
        tmp >>= 4;
        tile_2 <<= 4;
    }
    res = res / num_open;

    if (state.curdepth < CACHE_DEPTH_LIMIT) {
        trans_table_entry_t entry = {static_cast<uint8_t>(state.curdepth), res};
        state.trans_table[board] = entry;
    }

    return res;
}
static float score_move_node(table_data_t *table, eval_state &state, board_t board, float cprob) {
    float best = 0.0f;
    state.curdepth++;
    for (int move = 0; move < 4; ++move) {
        board_t newboard = execute_move(table, move, board);
        state.moves_evaled++;

        if (board != newboard) {
            best = max(best, score_tilechoose_node(table, state, newboard, cprob));
        }
    }
    state.curdepth--;

    return best;
}
static float _score_toplevel_move(table_data_t *table, eval_state &state, board_t board, int move) {
    //int maxrank = get_max_rank(board);
    board_t newboard = execute_move(table, move, board);
    if (board == newboard) {
        return 0;
	}
    return score_tilechoose_node(table, state, newboard, 1.0f) + 1e-6;
}
float score_toplevel_move(table_data_t *table, board_t board, int move) {
    float res;
    //struct timeval start, finish;
    //double elapsed;
    eval_state state;
    state.depth_limit = max(3, count_distinct_tiles(board) - 2);

    //gettimeofday(&start, NULL);
    res = _score_toplevel_move(table, state, board, move);
    //gettimeofday(&finish, NULL);

	/*
    elapsed = (finish.tv_sec - start.tv_sec);
    elapsed += (finish.tv_usec - start.tv_usec) / 1000000.0;

    printf("Move %d: result %f: eval'd %ld moves (%d cache hits, %d cache size) in %.2f seconds (maxdepth=%d)\n", move, res,
        state.moves_evaled, state.cachehits, (int)state.trans_table.size(), elapsed, state.maxdepth);
	*/
    return res;
}

static inline bool has_move(table_data_t *table, board_t board)
{
    for (int move = 0; move < 4; move++) {
        if(execute_move(table, move, board) != board){
             return true;
        }
    }
    return false;
}

/* Find the best move for a given board. */
int find_best_move(table_data_t *table, board_t board) {
    int move;
    float best = 0;
    int bestmove = -1;
    if(!has_move(table,board)){
        return -1;
    }

    std::future<float> tasks[4];

    //print_board(board);
    //printf("Current scores: heur %.0f, actual %.0f\n", score_heur_board(board), score_board(board));

    for(move=0; move<4; move++) {
	tasks[move]=std::async(std::launch::async,[table,board,move](){
	    return score_toplevel_move(table,board,move);
	});
    }
    for (move = 0; move < 4; move++) {
        float res = tasks[move].get();
        if (res > best) {
            best = res;
            bestmove = move;
        }
    }
    return bestmove;
}
