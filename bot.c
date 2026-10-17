#include "bot.h"

#include <stdlib.h>

void bot_init(unsigned seed) {
    srand(seed);
}

/* 当前该下的一方落在 (r,c) 是否立刻连成三子 */
static int wins_now(const Game *g, int r, int c) {
    Game t = *g;
    t.cell[r][c] = g->cur;
    return game_winner(&t) == g->cur;
}

/* 从空位里随机挑一个（蓄水池抽样，一次遍历就够） */
static void pick_random(const Game *g, int *pr, int *pc) {
    int n = 0, br = 0, bc = 0;
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) {
            if (g->cell[r][c] != EMPTY) continue;
            if (rand() % ++n == 0) { br = r; bc = c; }
        }
    *pr = br;
    *pc = bc;
}

/* 从 me 的视角给局面打分：me 赢为正、输为负，越早赢分越高。
   轮到 me 走就取最大值，轮到对手走就取最小值。 */
static int eval(Game *g, int me, int depth) {
    int w = game_winner(g);
    if (w == me)    return 10 - depth;   /* me 已连成三子 */
    if (w != EMPTY) return depth - 10;   /* 对手连成了 */

    int my_turn = (g->cur == me);
    int best = my_turn ? -1000 : 1000;
    int any = 0;

    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) {
            if (g->cell[r][c] != EMPTY) continue;
            any = 1;

            g->cell[r][c] = g->cur;
            int saved = g->cur;
            g->cur = (saved == WHITE) ? BLACK : WHITE;
            int v = eval(g, me, depth + 1);
            g->cur = saved;
            g->cell[r][c] = EMPTY;

            if (my_turn) { if (v > best) best = v; }
            else         { if (v < best) best = v; }
        }

    return any ? best : 0;   /* 一个空位都没有 → 平局 */
}

void bot_move(const Game *g, int level, int *pr, int *pc) {
    int me = g->cur;

    if (level == BOT_EASY) {
        /* 能赢就赢，但绝不堵对手 —— 所以玩家总能赢 */
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 3; c++)
                if (g->cell[r][c] == EMPTY && wins_now(g, r, c)) {
                    *pr = r;
                    *pc = c;
                    return;
                }
        pick_random(g, pr, pc);
        return;
    }

    /* 困难：挑分值最高的走法；同分的随机选一个，免得每局开头都一模一样 */
    Game sim = *g;
    int best = -1000, ties = 0, br = -1, bc = -1;

    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) {
            if (sim.cell[r][c] != EMPTY) continue;

            sim.cell[r][c] = me;
            sim.cur = (me == WHITE) ? BLACK : WHITE;
            int v = eval(&sim, me, 1);
            sim.cell[r][c] = EMPTY;

            if (v > best)       { best = v; ties = 1; br = r; bc = c; }
            else if (v == best && rand() % ++ties == 0) { br = r; bc = c; }
        }

    *pr = br;
    *pc = bc;
}
