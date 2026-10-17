#include "game.h"

/* 八条连线：3 行 + 3 列 + 2 对角线，每条是三个 (行, 列) */
static const int WIN_LINES[8][3][2] = {
    {{0, 0}, {0, 1}, {0, 2}},
    {{1, 0}, {1, 1}, {1, 2}},
    {{2, 0}, {2, 1}, {2, 2}},
    {{0, 0}, {1, 0}, {2, 0}},
    {{0, 1}, {1, 1}, {2, 1}},
    {{0, 2}, {1, 2}, {2, 2}},
    {{0, 0}, {1, 1}, {2, 2}},
    {{0, 2}, {1, 1}, {2, 0}},
};

void game_init(Game *g) {
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++)
            g->cell[r][c] = EMPTY;
    g->cur = WHITE;
    g->cr = g->cc = 1;
    g->winner = EMPTY;
    g->moves = 0;
}

/* 返回已连成三子的那一方，没有则返回 EMPTY */
int game_winner(const Game *g) {
    for (int i = 0; i < 8; i++) {
        int a = g->cell[WIN_LINES[i][0][0]][WIN_LINES[i][0][1]];
        if (a == EMPTY) continue;
        if (a == g->cell[WIN_LINES[i][1][0]][WIN_LINES[i][1][1]] &&
            a == g->cell[WIN_LINES[i][2][0]][WIN_LINES[i][2][1]])
            return a;
    }
    return EMPTY;
}

void game_place(Game *g, int r, int c) {
    if (g->winner != EMPTY) return;      /* 已终局 */
    if (g->cell[r][c] != EMPTY) return;  /* 该格已有子 */

    g->cell[r][c] = g->cur;
    g->moves++;

    if (game_winner(g) == g->cur) {
        g->winner = g->cur;
    } else if (g->moves == 9) {
        g->winner = DRAW;
    } else {
        g->cur = (g->cur == WHITE) ? BLACK : WHITE;
    }
}
